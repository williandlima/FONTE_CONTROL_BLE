package com.williandlima.fontecontrol.ui

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.williandlima.fontecontrol.ble.BleUartClient
import com.williandlima.fontecontrol.ble.BleUartClient.ConnState
import com.williandlima.fontecontrol.model.Incoming
import com.williandlima.fontecontrol.model.Measurement
import com.williandlima.fontecontrol.model.Protocol
import com.williandlima.fontecontrol.model.PsuSettings
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.math.abs

data class Stats(
    val vMin: Double = Double.NaN, val vMax: Double = Double.NaN,
    val iMin: Double = Double.NaN, val iMax: Double = Double.NaN,
    val ah: Double = 0.0, val wh: Double = 0.0,
    val sinceMs: Long = System.currentTimeMillis(),
)

data class UiState(
    val conn: ConnState = ConnState.DISCONNECTED,
    val link: Boolean = false,
    val idn: String? = null,
    val settings: PsuSettings = PsuSettings(),
    val meas: Measurement? = null,
    val history: List<Measurement> = emptyList(),
    val stats: Stats = Stats(),
    val console: List<String> = emptyList(),
    val message: String? = null,
    val busy: Int = 0,
) {
    /** CC quando a corrente medida esta no limite programado. */
    val ccMode: Boolean
        get() {
            val m = meas ?: return false
            return m.out && settings.iset > 0 && abs(m.i - settings.iset) <= maxOf(0.003, settings.iset * 0.005)
        }
}

class PsuViewModel(app: Application) : AndroidViewModel(app) {

    val ble = BleUartClient(app, viewModelScope)

    private val _ui = MutableStateFlow(UiState())
    val ui: StateFlow<UiState> = _ui

    private var nextId = 1
    private val pending = HashMap<Int, String>()
    private val history = ArrayDeque<Measurement>()
    private val maxHistory = 7200
    private val tf = SimpleDateFormat("HH:mm:ss", Locale.getDefault())

    init {
        viewModelScope.launch { ble.lines.collect { onLine(it) } }
        viewModelScope.launch {
            ble.state.collect { s ->
                _ui.update { it.copy(conn = s, link = if (s == ConnState.CONNECTED) it.link else false) }
                if (s == ConnState.CONNECTED) {
                    log("BLE conectado")
                    send("status")
                } else if (s == ConnState.DISCONNECTED) {
                    _ui.update { it.copy(busy = 0) }
                    pending.clear()
                }
            }
        }
    }

    // ------------------------------------------------------------- envio
    private fun send(cmd: String, vararg args: Pair<String, Any?>, logIt: Boolean = false) {
        val id = nextId++
        val line = Protocol.cmd(cmd, id, *args)
        if (ble.sendLine(line)) {
            pending[id] = cmd
            _ui.update { it.copy(busy = pending.size) }
            if (logIt) log("> $line")
        } else {
            toast("Nao conectado")
        }
    }

    fun output(on: Boolean) = send("out", "on" to on)
    fun setVoltage(v: Double) = send("volt", "v" to v)
    fun setCurrent(a: Double) = send("curr", "a" to a)
    fun apply(v: Double, a: Double) = send("apply", "v" to v, "a" to a)
    fun setRange(r: String) = send("range", "r" to r)
    fun setOvp(level: Double?, on: Boolean?) = send("ovp", "v" to level, "on" to on)
    fun setOcp(level: Double?, on: Boolean?) = send("ocp", "a" to level, "on" to on)
    fun clearProtection() = send("prot_clear")
    fun save(n: Int) = send("save", "n" to n)
    fun recall(n: Int) = send("recall", "n" to n)
    fun display(on: Boolean) = send("disp", "on" to on)
    fun displayText(s: String) = send("text", "s" to s)
    fun displayTextClear() = send("text_clear")
    fun beep() = send("beep")
    fun reset() = send("reset")
    fun local() = send("local")
    fun remote(lock: Boolean) = send("remote", "lock" to lock)
    fun triggerSource(s: String) = send("trig_src", "s" to s)
    fun triggerDelay(s: Double) = send("trig_del", "s" to s)
    fun triggerLevels(v: Double?, a: Double?) = send("trig_lvl", "v" to v, "a" to a)
    fun initiate() = send("init")
    fun trigger() = send("trg")
    fun refresh() = send("status")
    fun readErrors() = send("err", logIt = true)
    fun relink() = send("link")
    fun poll(ms: Int) = send("poll", "ms" to ms)
    fun scpi(q: String) {
        log("> $q")
        send("scpi", "q" to q)
    }

    fun nudgeVoltage(delta: Double) {
        val s = _ui.value.settings
        setVoltage((s.vset + delta).coerceIn(0.0, s.vMax))
    }

    fun nudgeCurrent(delta: Double) {
        val s = _ui.value.settings
        setCurrent((s.iset + delta).coerceIn(0.0, s.iMax))
    }

    fun resetStats() {
        history.clear()
        _ui.update { it.copy(stats = Stats(), history = emptyList()) }
    }

    fun consumeMessage() = _ui.update { it.copy(message = null) }

    fun historyCsv(): String {
        val sb = StringBuilder("tempo_ms,tensao_V,corrente_A,potencia_W,saida\n")
        val t0 = history.firstOrNull()?.tMs ?: 0L
        for (m in history) {
            sb.append(m.tMs - t0).append(',')
                .append("%.5f,%.6f,%.5f".format(Locale.US, m.v, m.i, m.p)).append(',')
                .append(if (m.out) 1 else 0).append('\n')
        }
        return sb.toString()
    }

    // ---------------------------------------------------------- recepcao
    private fun onLine(line: String) {
        when (val msg = Protocol.parse(line)) {
            is Incoming.Hello -> _ui.update {
                it.copy(link = msg.link, idn = msg.idn ?: it.idn,
                    settings = it.settings.copy(pollMs = msg.pollMs))
            }
            is Incoming.Link -> {
                _ui.update { it.copy(link = msg.ok, idn = msg.idn ?: it.idn) }
                if (msg.ok) log("Fonte: ${msg.idn}") else log("Fonte sem resposta: ${msg.err ?: ""}")
                if (!msg.ok) toast("Fonte nao responde (RS-232)")
            }
            is Incoming.Meas -> onMeasurement(msg.m)
            is Incoming.Status -> _ui.update { it.copy(settings = msg.s, link = true) }
            is Incoming.Resp -> {
                val cmd = msg.id?.let { pending.remove(it) } ?: msg.cmd
                _ui.update { it.copy(busy = pending.size) }
                if (!msg.ok) {
                    log("! $cmd: ${msg.err}")
                    toast(msg.err ?: "Erro")
                } else if (msg.data != null && cmd != "poll") {
                    log("< ${msg.data}")
                }
            }
            is Incoming.Error -> {
                log("! SCPI ${msg.code}: ${msg.msg}")
                toast("Erro ${msg.code}: ${msg.msg}")
            }
            null -> log("? $line")
        }
    }

    private fun onMeasurement(m: Measurement) {
        val prev = history.lastOrNull()
        history.addLast(m)
        while (history.size > maxHistory) history.removeFirst()
        _ui.update { st ->
            var s = st.stats
            s = s.copy(
                vMin = if (s.vMin.isNaN()) m.v else minOf(s.vMin, m.v),
                vMax = if (s.vMax.isNaN()) m.v else maxOf(s.vMax, m.v),
                iMin = if (s.iMin.isNaN()) m.i else minOf(s.iMin, m.i),
                iMax = if (s.iMax.isNaN()) m.i else maxOf(s.iMax, m.i),
            )
            if (prev != null) {
                val dtH = (m.tMs - prev.tMs).coerceIn(0, 5000) / 3_600_000.0
                s = s.copy(ah = s.ah + m.i * dtH, wh = s.wh + m.p * dtH)
            }
            st.copy(
                meas = m, stats = s, history = history.toList(),
                settings = st.settings.copy(out = m.out),
            )
        }
        if (m.ovpTrip && prev?.ovpTrip != true) toast("OVP disparou!")
        if (m.ocpTrip && prev?.ocpTrip != true) toast("OCP disparou!")
    }

    private fun log(s: String) {
        val l = "${tf.format(Date())}  $s"
        _ui.update { it.copy(console = (it.console + l).takeLast(300)) }
    }

    private fun toast(s: String) = _ui.update { it.copy(message = s) }

    override fun onCleared() {
        ble.disconnect()
        super.onCleared()
    }
}
