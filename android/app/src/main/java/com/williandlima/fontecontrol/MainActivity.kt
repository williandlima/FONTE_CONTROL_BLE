package com.williandlima.fontecontrol

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import com.williandlima.fontecontrol.ui.AppRoot
import com.williandlima.fontecontrol.ui.FonteTheme
import com.williandlima.fontecontrol.ui.PsuViewModel

class MainActivity : ComponentActivity() {

    private val vm: PsuViewModel by viewModels()

    private val requiredPermissions: Array<String> =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
        } else {
            arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }

    private fun hasPermissions() = requiredPermissions.all {
        ContextCompat.checkSelfPermission(this, it) == PackageManager.PERMISSION_GRANTED
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            FonteTheme {
                Surface(Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
                    PermissionGate { AppRoot(vm) }
                }
            }
        }
    }

    @Composable
    private fun PermissionGate(content: @Composable () -> Unit) {
        var granted by remember { mutableStateOf(hasPermissions()) }
        var btOn by remember { mutableStateOf(vm.ble.isEnabled) }
        val permLauncher = rememberLauncherForActivityResult(
            ActivityResultContracts.RequestMultiplePermissions()
        ) { granted = hasPermissions() }
        val btLauncher = rememberLauncherForActivityResult(
            ActivityResultContracts.StartActivityForResult()
        ) { btOn = vm.ble.isEnabled }

        LaunchedEffect(Unit) { if (!granted) permLauncher.launch(requiredPermissions) }
        LaunchedEffect(granted) {
            if (granted && !btOn) btLauncher.launch(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE))
        }

        when {
            !granted -> Message("O app precisa da permissao de Bluetooth para encontrar a ponte E3634A-BLE.",
                "Conceder permissao") { permLauncher.launch(requiredPermissions) }
            !btOn -> Message("Ative o Bluetooth para continuar.", "Ativar Bluetooth") {
                btLauncher.launch(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE))
            }
            else -> content()
        }
    }

    @Composable
    private fun Message(text: String, button: String, onClick: () -> Unit) {
        Column(
            Modifier.fillMaxSize().padding(32.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp, Alignment.CenterVertically),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text(text)
            Button(onClick = onClick) { Text(button) }
        }
    }
}
