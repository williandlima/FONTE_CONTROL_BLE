package com.williandlima.fontecontrol.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.Build
import android.os.ParcelUuid
import android.util.Log
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull
import java.util.UUID

/**
 * Cliente BLE para o Nordic UART Service (NUS) exposto pelo ESP32-C3.
 * Envia/recebe linhas de texto terminadas em '\n' (JSON, ver docs/PROTOCOLO.md).
 */
@SuppressLint("MissingPermission") // permissoes verificadas na MainActivity
class BleUartClient(private val ctx: Context, private val scope: CoroutineScope) {

    companion object {
        private const val TAG = "BleUart"
        val NUS_SERVICE: UUID = UUID.fromString("6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
        val NUS_RX: UUID = UUID.fromString("6E400002-B5A3-F393-E0A9-E50E24DCCA9E") // app -> ESP
        val NUS_TX: UUID = UUID.fromString("6E400003-B5A3-F393-E0A9-E50E24DCCA9E") // ESP -> app
        val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
        const val NAME_PREFIX = "E3634A"
    }

    enum class ConnState { DISCONNECTED, CONNECTING, CONNECTED }

    data class Device(val name: String, val address: String, val rssi: Int)

    private val _state = MutableStateFlow(ConnState.DISCONNECTED)
    val state: StateFlow<ConnState> = _state

    private val _devices = MutableStateFlow<List<Device>>(emptyList())
    val devices: StateFlow<List<Device>> = _devices

    private val _scanning = MutableStateFlow(false)
    val scanning: StateFlow<Boolean> = _scanning

    private val _lines = MutableSharedFlow<String>(extraBufferCapacity = 512)
    val lines: SharedFlow<String> = _lines

    private val adapter: BluetoothAdapter?
        get() = ctx.getSystemService(BluetoothManager::class.java)?.adapter

    val isEnabled: Boolean get() = adapter?.isEnabled == true

    private var gatt: BluetoothGatt? = null
    private var rxChar: BluetoothGattCharacteristic? = null
    @Volatile private var mtu = 23
    private val inBuf = StringBuilder()
    private var writeQueue = Channel<ByteArray>(Channel.UNLIMITED)
    @Volatile private var writeAck: CompletableDeferred<Boolean>? = null
    private var writerJob: Job? = null
    private var scanJob: Job? = null

    /** Endereco do ultimo dispositivo; usado para reconexao automatica. */
    var lastAddress: String? = null
        private set
    @Volatile private var userDisconnect = false

    // ------------------------------------------------------------------ scan
    private val scanCb = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val rec = result.scanRecord
            val name = result.device.name ?: rec?.deviceName ?: ""
            val hasNus = rec?.serviceUuids?.contains(ParcelUuid(NUS_SERVICE)) == true
            if (!hasNus && !name.startsWith(NAME_PREFIX)) return
            val d = Device(name.ifEmpty { "(sem nome)" }, result.device.address, result.rssi)
            _devices.value = (_devices.value.filter { it.address != d.address } + d)
                .sortedByDescending { it.rssi }
        }

        override fun onScanFailed(errorCode: Int) {
            Log.w(TAG, "scan falhou: $errorCode")
            _scanning.value = false
        }
    }

    fun startScan(durationMs: Long = 10_000) {
        val scanner = adapter?.bluetoothLeScanner ?: return
        stopScan()
        _devices.value = emptyList()
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()
        scanner.startScan(null, settings, scanCb)
        _scanning.value = true
        scanJob = scope.launch {
            delay(durationMs)
            stopScan()
        }
    }

    fun stopScan() {
        scanJob?.cancel()
        scanJob = null
        if (_scanning.value) {
            try {
                adapter?.bluetoothLeScanner?.stopScan(scanCb)
            } catch (e: Exception) {
                Log.w(TAG, "stopScan", e)
            }
        }
        _scanning.value = false
    }

    // -------------------------------------------------------------- conexao
    fun connect(address: String) {
        stopScan()
        closeGatt()
        val dev: BluetoothDevice = adapter?.getRemoteDevice(address) ?: return
        lastAddress = address
        userDisconnect = false
        _state.value = ConnState.CONNECTING
        gatt = dev.connectGatt(ctx, false, gattCb, BluetoothDevice.TRANSPORT_LE)
    }

    fun disconnect() {
        userDisconnect = true
        gatt?.disconnect()
        closeGatt()
        _state.value = ConnState.DISCONNECTED
    }

    private fun closeGatt() {
        writerJob?.cancel()
        writerJob = null
        writeAck?.complete(false)
        writeQueue.close()
        writeQueue = Channel(Channel.UNLIMITED)
        gatt?.close()
        gatt = null
        rxChar = null
        synchronized(inBuf) { inBuf.setLength(0) }
    }

    private fun scheduleReconnect() {
        val addr = lastAddress ?: return
        if (userDisconnect) return
        scope.launch {
            delay(2000)
            if (!userDisconnect && _state.value == ConnState.DISCONNECTED) connect(addr)
        }
    }

    /** Envia uma linha (o '\n' e adicionado aqui). */
    fun sendLine(line: String): Boolean {
        if (_state.value != ConnState.CONNECTED) return false
        return writeQueue.trySend((line + "\n").toByteArray()).isSuccess
    }

    private fun startWriter() {
        val q = writeQueue
        writerJob = scope.launch {
            for (data in q) {
                val step = (mtu - 3).coerceAtLeast(20)
                var off = 0
                while (off < data.size) {
                    val chunk = data.copyOfRange(off, minOf(off + step, data.size))
                    if (!writeChunk(chunk)) {
                        Log.w(TAG, "falha de escrita")
                        break
                    }
                    off += step
                }
            }
        }
    }

    private suspend fun writeChunk(chunk: ByteArray): Boolean {
        val g = gatt ?: return false
        val ch = rxChar ?: return false
        val ack = CompletableDeferred<Boolean>()
        writeAck = ack
        val started = if (Build.VERSION.SDK_INT >= 33) {
            g.writeCharacteristic(ch, chunk, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) ==
                BluetoothGatt.GATT_SUCCESS
        } else {
            legacyWrite(g, ch, chunk)
        }
        if (!started) return false
        return withTimeoutOrNull(3000) { ack.await() } ?: false
    }

    @Suppress("DEPRECATION")
    private fun legacyWrite(g: BluetoothGatt, ch: BluetoothGattCharacteristic, data: ByteArray): Boolean {
        ch.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        ch.value = data
        return g.writeCharacteristic(ch)
    }

    @Suppress("DEPRECATION")
    private fun legacyWriteDescriptor(g: BluetoothGatt, d: BluetoothGattDescriptor, data: ByteArray) {
        d.value = data
        g.writeDescriptor(d)
    }

    private fun onBytes(value: ByteArray) {
        val text = String(value, Charsets.UTF_8)
        val out = mutableListOf<String>()
        synchronized(inBuf) {
            inBuf.append(text)
            while (true) {
                val i = inBuf.indexOf("\n")
                if (i < 0) break
                out += inBuf.substring(0, i).trim()
                inBuf.delete(0, i + 1)
            }
            if (inBuf.length > 8192) inBuf.setLength(0)
        }
        out.filter { it.isNotEmpty() }.forEach { _lines.tryEmit(it) }
    }

    private val gattCb = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (g != gatt) return
            if (newState == BluetoothProfile.STATE_CONNECTED && status == BluetoothGatt.GATT_SUCCESS) {
                g.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH)
                if (!g.requestMtu(247)) g.discoverServices()
            } else {
                Log.i(TAG, "desconectado status=$status")
                closeGatt()
                _state.value = ConnState.DISCONNECTED
                scheduleReconnect()
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, newMtu: Int, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) mtu = newMtu
            g.discoverServices()
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            val svc = g.getService(NUS_SERVICE)
            val tx = svc?.getCharacteristic(NUS_TX)
            rxChar = svc?.getCharacteristic(NUS_RX)
            if (tx == null || rxChar == null) {
                Log.e(TAG, "servico NUS nao encontrado")
                g.disconnect()
                return
            }
            g.setCharacteristicNotification(tx, true)
            val cccd = tx.getDescriptor(CCCD) ?: return
            val en = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            if (Build.VERSION.SDK_INT >= 33) {
                g.writeDescriptor(cccd, en)
            } else {
                legacyWriteDescriptor(g, cccd, en)
            }
        }

        override fun onDescriptorWrite(g: BluetoothGatt, d: BluetoothGattDescriptor, status: Int) {
            if (d.uuid == CCCD && status == BluetoothGatt.GATT_SUCCESS) {
                startWriter()
                _state.value = ConnState.CONNECTED
            } else {
                g.disconnect()
            }
        }

        override fun onCharacteristicWrite(
            g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int
        ) {
            writeAck?.complete(status == BluetoothGatt.GATT_SUCCESS)
        }

        // Android 13+
        override fun onCharacteristicChanged(
            g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray
        ) {
            if (c.uuid == NUS_TX) onBytes(value)
        }

        // Android 8..12
        @Deprecated("API < 33")
        @Suppress("DEPRECATION")
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) {
            if (Build.VERSION.SDK_INT >= 33 || c.uuid != NUS_TX) return
            val v: ByteArray = c.value ?: return
            onBytes(v)
        }
    }
}
