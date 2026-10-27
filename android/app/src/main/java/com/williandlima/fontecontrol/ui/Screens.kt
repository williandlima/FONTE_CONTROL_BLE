@file:OptIn(ExperimentalMaterial3Api::class)

package com.williandlima.fontecontrol.ui

import android.content.Intent
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Share
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.ScrollableTabRow
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Switch
import androidx.compose.material3.Tab
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.williandlima.fontecontrol.ble.BleUartClient.ConnState

/** Aceita virgula ou ponto como separador decimal. */
private fun parseNum(s: String): Double? = s.trim().replace(',', '.').toDoubleOrNull()

@Composable
fun AppRoot(vm: PsuViewModel) {
    val ui by vm.ui.collectAsStateWithLifecycle()
    val snack = remember { SnackbarHostState() }
    LaunchedEffect(ui.message) {
        ui.message?.let {
            snack.showSnackbar(it)
            vm.consumeMessage()
        }
    }
    val fwTxt = ui.fw?.let { " · FW $it" } ?: ""
    val statusText = when {
        ui.conn != ConnState.CONNECTED -> ui.conn.label()
        !ui.link -> "BLE ok · fonte sem resposta$fwTxt"
        else -> (ui.idn ?: "Conectado") + fwTxt
    }
    val statusColor = when {
        ui.conn == ConnState.CONNECTING -> AmpColor
        ui.conn != ConnState.CONNECTED -> TextMuted
        !ui.link -> AmpColor
        else -> OnColor
    }
    Scaffold(
        containerColor = Bg,
        topBar = {
            TopAppBar(
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = Panel,
                    titleContentColor = TextMain,
                    actionIconContentColor = TextMuted,
                ),
                title = {
                    Column {
                        Text("FONTE E3634A", fontWeight = FontWeight.Bold, fontSize = 16.sp, letterSpacing = 1.2.sp)
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Box(Modifier.size(8.dp).background(statusColor, CircleShape))
                            Spacer(Modifier.width(6.dp))
                            Text(statusText, fontSize = 11.sp, color = TextMuted, maxLines = 1)
                        }
                    }
                },
                actions = {
                    if (ui.busy > 0) {
                        CircularProgressIndicator(Modifier.size(18.dp), strokeWidth = 2.dp)
                        Spacer(Modifier.width(8.dp))
                    }
                    if (ui.conn == ConnState.CONNECTED) {
                        IconButton(onClick = vm::refresh) { Icon(Icons.Default.Refresh, "Atualizar") }
                        IconButton(onClick = { vm.ble.disconnect() }) { Icon(Icons.Default.Close, "Desconectar") }
                    }
                },
            )
        },
        snackbarHost = { SnackbarHost(snack) },
    ) { pad ->
        Box(Modifier.padding(pad).fillMaxSize()) {
            if (ui.conn == ConnState.CONNECTED) ControlScreen(vm, ui) else ScanScreen(vm, ui.conn)
        }
    }
}

private fun ConnState.label() = when (this) {
    ConnState.DISCONNECTED -> "Desconectado"
    ConnState.CONNECTING -> "Conectando..."
    ConnState.CONNECTED -> "Conectado"
}

// =================================================================== SCAN
@Composable
fun ScanScreen(vm: PsuViewModel, conn: ConnState) {
    val devices by vm.ble.devices.collectAsStateWithLifecycle()
    val scanning by vm.ble.scanning.collectAsStateWithLifecycle()
    LaunchedEffect(Unit) { if (conn == ConnState.DISCONNECTED) vm.ble.startScan() }
    Column(Modifier.fillMaxSize().padding(16.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("DISPOSITIVOS BLE", fontSize = 12.sp, letterSpacing = 1.2.sp, fontWeight = FontWeight.SemiBold,
                color = TextMuted, modifier = Modifier.weight(1f))
            if (scanning) CircularProgressIndicator(Modifier.size(20.dp), strokeWidth = 2.dp)
            else IconButton(onClick = { vm.ble.startScan() }) { Icon(Icons.Default.Search, "Procurar") }
        }
        if (conn == ConnState.CONNECTING) {
            Row(Modifier.padding(vertical = 12.dp), verticalAlignment = Alignment.CenterVertically) {
                CircularProgressIndicator(Modifier.size(20.dp), strokeWidth = 2.dp)
                Spacer(Modifier.width(12.dp))
                Text("Conectando a ${vm.ble.lastAddress ?: ""}...")
                Spacer(Modifier.weight(1f))
                TextButton(onClick = { vm.ble.disconnect() }) { Text("Cancelar") }
            }
        }
        if (devices.isEmpty() && !scanning) {
            Text(
                "Nenhuma ponte \"E3634A-BLE\" encontrada. Verifique se o ESP32 está " +
                    "alimentado (LED piscando lento) e toque na lupa.",
                Modifier.padding(top = 24.dp),
                color = TextMuted,
            )
        }
        LazyColumn {
            items(devices, key = { it.address }) { d ->
                PanelCard(
                    Modifier.fillMaxWidth().padding(vertical = 4.dp).clickable { vm.ble.connect(d.address) }
                ) {
                    Row(Modifier.padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
                        Column(Modifier.weight(1f)) {
                            Text(d.name, fontWeight = FontWeight.Bold)
                            Text(d.address, style = MaterialTheme.typography.bodySmall, color = TextMuted)
                        }
                        Text("${d.rssi} dBm", style = MaterialTheme.typography.bodySmall, color = TextMuted)
                    }
                }
            }
        }
    }
}

// ================================================================ CONTROLE
@Composable
fun ControlScreen(vm: PsuViewModel, ui: UiState) {
    var tab by rememberSaveable { mutableIntStateOf(0) }
    val tabs = listOf("Painel", "Gráfico", "Proteção", "Mais", "SCPI")
    Column(Modifier.fillMaxSize()) {
        ScrollableTabRow(
            selectedTabIndex = tab, edgePadding = 8.dp,
            containerColor = Panel, contentColor = Accent,
        ) {
            tabs.forEachIndexed { i, t ->
                Tab(
                    selected = tab == i, onClick = { tab = i },
                    selectedContentColor = Accent, unselectedContentColor = TextMuted,
                    text = { Text(t.uppercase(), maxLines = 1, fontSize = 12.sp, letterSpacing = 0.8.sp) },
                )
            }
        }
        if (!ui.link) {
            Row(
                Modifier.fillMaxWidth().background(AlarmColor.copy(alpha = 0.16f))
                    .padding(horizontal = 14.dp, vertical = 2.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text("Fonte não responde no RS-232", Modifier.weight(1f), color = Color(0xFFFFB4B6), fontSize = 13.sp)
                TextButton(onClick = vm::relink) { Text("Tentar") }
            }
        }
        when (tab) {
            0 -> PanelTab(vm, ui)
            1 -> ChartTab(vm, ui)
            2 -> ProtectionTab(vm, ui)
            3 -> MoreTab(vm, ui)
            else -> ConsoleTab(vm, ui)
        }
    }
}

@Composable
private fun Section(title: String, content: @Composable () -> Unit) {
    PanelCard(Modifier.fillMaxWidth().padding(vertical = 6.dp)) {
        Column(Modifier.padding(14.dp)) {
            Text(
                title.uppercase(), fontSize = 11.sp, letterSpacing = 1.2.sp,
                fontWeight = FontWeight.SemiBold, color = TextMuted,
            )
            Spacer(Modifier.height(10.dp))
            content()
        }
    }
}

@Composable
private fun Readout(caption: String, value: Double?, unit: String, color: Color, fmt: String, big: Boolean = true) {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.Bottom) {
        Text(
            caption, fontSize = 11.sp, letterSpacing = 1.2.sp, color = TextMuted,
            modifier = Modifier.weight(1f).padding(bottom = if (big) 10.dp else 4.dp),
        )
        Text(
            value?.let { fmt.format(it) } ?: "--.----",
            fontFamily = FontFamily.Monospace,
            fontSize = if (big) 44.sp else 24.sp,
            fontWeight = FontWeight.Medium,
            color = color,
        )
        Text(
            unit, fontSize = if (big) 20.sp else 14.sp, color = color.copy(alpha = 0.8f),
            modifier = Modifier.width(if (big) 30.dp else 24.dp)
                .padding(start = 4.dp, bottom = if (big) 8.dp else 3.dp),
        )
    }
}

@Composable
private fun Badge(text: String, color: Color) {
    val pill = RoundedCornerShape(50)
    Text(
        text,
        Modifier.background(color.copy(alpha = 0.14f), pill)
            .border(1.dp, color.copy(alpha = 0.55f), pill)
            .padding(horizontal = 10.dp, vertical = 3.dp),
        color = color, fontWeight = FontWeight.SemiBold, fontSize = 11.sp, letterSpacing = 0.8.sp,
    )
}

@Composable
private fun NumberField(
    label: String,
    initial: Double,
    fmt: String,
    modifier: Modifier = Modifier,
    onSubmit: (Double) -> Unit,
) {
    var text by remember(initial) { mutableStateOf(fmt.format(initial)) }
    val focus = LocalFocusManager.current
    OutlinedTextField(
        value = text,
        onValueChange = { text = it },
        label = { Text(label) },
        singleLine = true,
        shape = RoundedCornerShape(10.dp),
        isError = parseNum(text) == null,
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal, imeAction = ImeAction.Done),
        keyboardActions = KeyboardActions(onDone = {
            parseNum(text)?.let(onSubmit)
            focus.clearFocus()
        }),
        modifier = modifier,
    )
}

@Composable
private fun SetpointRow(
    label: String, unit: String, value: Double, fmt: String, steps: List<Double>,
    onSet: (Double) -> Unit, onNudge: (Double) -> Unit,
) {
    var text by remember(value) { mutableStateOf(fmt.format(value)) }
    val focus = LocalFocusManager.current
    Column {
        Row(verticalAlignment = Alignment.CenterVertically) {
            OutlinedTextField(
                value = text, onValueChange = { text = it },
                label = { Text("$label ($unit)") }, singleLine = true,
                shape = RoundedCornerShape(10.dp),
                isError = parseNum(text) == null,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal, imeAction = ImeAction.Done),
                keyboardActions = KeyboardActions(onDone = { parseNum(text)?.let(onSet); focus.clearFocus() }),
                modifier = Modifier.weight(1f),
            )
            Spacer(Modifier.width(8.dp))
            Button(onClick = { parseNum(text)?.let(onSet); focus.clearFocus() }) { Text("Set") }
        }
        Row(Modifier.fillMaxWidth().padding(top = 4.dp), horizontalArrangement = Arrangement.SpaceBetween) {
            for (s in steps.reversed()) SmallStep("-${trimNum(s)}") { onNudge(-s) }
            for (s in steps) SmallStep("+${trimNum(s)}") { onNudge(s) }
        }
    }
}

private fun trimNum(d: Double) = if (d >= 1) "%.0f".format(d) else d.toString().trimEnd('0')

@Composable
private fun SmallStep(label: String, onClick: () -> Unit) {
    OutlinedButton(
        onClick = onClick, contentPadding = PaddingValues(horizontal = 6.dp, vertical = 0.dp),
        modifier = Modifier.height(34.dp), border = BorderStroke(1.dp, Line),
    ) { Text(label, fontSize = 12.sp, color = TextMain) }
}

// ----------------------------------------------------------------- Painel
@Composable
private fun PanelTab(vm: PsuViewModel, ui: UiState) {
    val m = ui.meas
    val s = ui.settings
    val tripped = m?.ovpTrip == true || m?.ocpTrip == true
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(12.dp)) {
        PanelCard(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(14.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Badge(if (s.out) "OUTPUT ON" else "OUTPUT OFF", if (s.out) OnColor else TextMuted)
                    Spacer(Modifier.width(6.dp))
                    if (s.out && !tripped) Badge(if (ui.ccMode) "CC" else "CV", if (ui.ccMode) AmpColor else VoltColor)
                    Spacer(Modifier.width(6.dp))
                    if (m?.ovpTrip == true) Badge("OVP", AlarmColor)
                    if (m?.ocpTrip == true) Badge("OCP", AlarmColor)
                    Spacer(Modifier.weight(1f))
                    Text(s.range, fontSize = 12.sp, color = TextMuted, letterSpacing = 1.sp)
                }
                Spacer(Modifier.height(12.dp))
                val lcd = RoundedCornerShape(10.dp)
                Column(
                    Modifier.fillMaxWidth().background(Lcd, lcd).border(1.dp, Line, lcd)
                        .padding(horizontal = 14.dp, vertical = 10.dp),
                ) {
                    Readout("TENSÃO", m?.v, "V", VoltColor, "%7.4f")
                    Readout("CORRENTE", m?.i, "A", AmpColor, "%7.5f")
                    HorizontalDivider(Modifier.padding(vertical = 6.dp), color = Line)
                    Readout("POTÊNCIA", m?.p, "W", TextMain, "%7.3f", big = false)
                }
            }
        }
        Spacer(Modifier.height(10.dp))
        Button(
            onClick = { vm.output(!s.out) },
            modifier = Modifier.fillMaxWidth().height(54.dp),
            colors = ButtonDefaults.buttonColors(
                containerColor = if (s.out) AlarmButton else OnButton, contentColor = Color.White,
            ),
        ) {
            Text(
                if (s.out) "DESLIGAR SAÍDA" else "LIGAR SAÍDA",
                fontSize = 16.sp, fontWeight = FontWeight.Bold, letterSpacing = 1.sp,
            )
        }

        if (tripped) {
            FilledTonalButton(onClick = vm::clearProtection, modifier = Modifier.fillMaxWidth().padding(top = 8.dp)) {
                Text("Limpar proteção (remova a causa antes)")
            }
        }

        Section("Ajustes (programado: %.3f V / %.4f A)".format(s.vset, s.iset)) {
            SetpointRow("Tensão", "V", s.vset, "%.3f", listOf(0.01, 0.1, 1.0),
                onSet = vm::setVoltage, onNudge = vm::nudgeVoltage)
            Spacer(Modifier.height(10.dp))
            SetpointRow("Corrente", "A", s.iset, "%.4f", listOf(0.001, 0.01, 0.1),
                onSet = vm::setCurrent, onNudge = vm::nudgeCurrent)
        }
        Section("Faixa") {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                FilterChip(selected = s.range == "P25V", onClick = { vm.setRange("P25V") },
                    label = { Text("25 V / 7 A") })
                FilterChip(selected = s.range == "P50V", onClick = { vm.setRange("P50V") },
                    label = { Text("50 V / 4 A") })
            }
        }
        Section("Memórias (*SAV / *RCL)") {
            MemoryRow(vm)
        }
    }
}

@Composable
private fun MemoryRow(vm: PsuViewModel) {
    var confirmSave by remember { mutableStateOf<Int?>(null) }
    Text("M1..M5 = carregar (*RCL)   |   S1..S5 = salvar (*SAV)", fontSize = 12.sp, color = TextMuted)
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
        for (n in 1..5) FilledTonalButton(onClick = { vm.recall(n) }, contentPadding = PaddingValues(0.dp),
            modifier = Modifier.width(56.dp)) { Text("M$n") }
    }
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
        for (n in 1..5) OutlinedButton(onClick = { confirmSave = n }, contentPadding = PaddingValues(0.dp),
            modifier = Modifier.width(56.dp)) { Text("S$n", fontSize = 12.sp) }
    }
    confirmSave?.let { n ->
        AlertDialog(
            onDismissRequest = { confirmSave = null },
            title = { Text("Salvar memória $n?") },
            text = { Text("A configuração atual sobrescreverá a memória $n da fonte.") },
            confirmButton = { TextButton(onClick = { vm.save(n); confirmSave = null }) { Text("Salvar") } },
            dismissButton = { TextButton(onClick = { confirmSave = null }) { Text("Cancelar") } },
        )
    }
}

// ---------------------------------------------------------------- Grafico
@Composable
private fun ChartTab(vm: PsuViewModel, ui: UiState) {
    val ctx = LocalContext.current
    val st = ui.stats
    val windows = listOf(30, 120, 600, 3600)
    var win by rememberSaveable { mutableIntStateOf(120) }
    val cut = (ui.history.lastOrNull()?.tMs ?: 0L) - win * 1000L
    val shown = ui.history.filter { it.tMs >= cut }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(12.dp)) {
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            for (w in windows) FilterChip(selected = win == w, onClick = { win = w },
                label = { Text(if (w < 60) "${w}s" else "${w / 60}min") })
        }
        PanelCard(Modifier.fillMaxWidth().padding(vertical = 8.dp)) {
            TrendChart(shown, Modifier.padding(8.dp), height = 260)
        }
        Section("Estatísticas") {
            StatLine("Tensão mín/máx", "%.4f / %.4f V".format(st.vMin, st.vMax))
            StatLine("Corrente mín/máx", "%.5f / %.5f A".format(st.iMin, st.iMax))
            StatLine("Carga", "%.4f Ah  (%.1f mAh)".format(st.ah, st.ah * 1000))
            StatLine("Energia", "%.4f Wh".format(st.wh))
            StatLine("Amostras", "${ui.history.size}")
        }
        Section("Taxa de leitura") {
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                for (ms in listOf(300, 500, 1000, 2000)) FilterChip(
                    selected = ui.settings.pollMs == ms,
                    onClick = { vm.poll(ms); vm.refresh() },
                    label = { Text("${ms}ms") })
            }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedButton(onClick = vm::resetStats) { Text("Zerar") }
            Button(onClick = {
                val send = Intent(Intent.ACTION_SEND).apply {
                    type = "text/csv"
                    putExtra(Intent.EXTRA_SUBJECT, "E3634A log")
                    putExtra(Intent.EXTRA_TEXT, vm.historyCsv())
                }
                ctx.startActivity(Intent.createChooser(send, "Exportar CSV"))
            }) {
                Icon(Icons.Default.Share, null)
                Spacer(Modifier.width(6.dp))
                Text("Exportar CSV")
            }
        }
    }
}

@Composable
private fun StatLine(k: String, v: String) {
    Row(Modifier.fillMaxWidth().padding(vertical = 2.dp)) {
        Text(k, Modifier.weight(1f), color = TextMuted, fontSize = 13.sp)
        Text(v, fontFamily = FontFamily.Monospace, fontSize = 13.sp)
    }
}

// --------------------------------------------------------------- Protecao
@Composable
private fun ProtectionTab(vm: PsuViewModel, ui: UiState) {
    val s = ui.settings
    val m = ui.meas
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(12.dp)) {
        Section("OVP – sobretensão") {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Habilitada", Modifier.weight(1f))
                Switch(checked = s.ovpEn, onCheckedChange = { vm.setOvp(null, it) })
            }
            NumberField("Nível OVP (1..55 V)", s.ovp, "%.2f", Modifier.fillMaxWidth()) { vm.setOvp(it, null) }
            if (m?.ovpTrip == true) Text("OVP DISPARADA", color = AlarmColor, fontWeight = FontWeight.Bold)
        }
        Section("OCP – sobrecorrente") {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Habilitada", Modifier.weight(1f))
                Switch(checked = s.ocpEn, onCheckedChange = { vm.setOcp(null, it) })
            }
            NumberField("Nível OCP (0..7.5 A)", s.ocp, "%.3f", Modifier.fillMaxWidth()) { vm.setOcp(it, null) }
            if (m?.ocpTrip == true) Text("OCP DISPARADA", color = AlarmColor, fontWeight = FontWeight.Bold)
        }
        Button(onClick = vm::clearProtection, Modifier.fillMaxWidth()) { Text("Limpar proteções (VOLT/CURR:PROT:CLE)") }
        Text(
            "Ao disparar, a saída é desligada. Remova a causa (carga/curto ou ajuste " +
                "acima do limite) antes de limpar.",
            fontSize = 12.sp, color = TextMuted, modifier = Modifier.padding(top = 8.dp),
        )
    }
}

// ------------------------------------------------------------------- Mais
@Composable
private fun MoreTab(vm: PsuViewModel, ui: UiState) {
    val s = ui.settings
    var text by rememberSaveable { mutableStateOf("") }
    var confirmReset by remember { mutableStateOf(false) }
    var lock by rememberSaveable { mutableStateOf(false) }
    val ctx = LocalContext.current
    val appVer = remember {
        runCatching { ctx.packageManager.getPackageInfo(ctx.packageName, 0).versionName }.getOrNull() ?: "?"
    }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(12.dp)) {
        Section("Display da fonte") {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Display ligado", Modifier.weight(1f))
                Switch(checked = s.disp, onCheckedChange = vm::display)
            }
            OutlinedTextField(text, { text = it.take(12) }, label = { Text("Mensagem (máx. 12)") },
                singleLine = true, shape = RoundedCornerShape(10.dp), modifier = Modifier.fillMaxWidth())
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.padding(top = 6.dp)) {
                Button(onClick = { vm.displayText(text) }) { Text("Mostrar") }
                OutlinedButton(onClick = vm::displayTextClear) { Text("Limpar") }
                OutlinedButton(onClick = vm::beep) { Text("Beep") }
            }
        }
        Section("Trigger") {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                FilterChip(selected = s.trigSrc == "BUS", onClick = { vm.triggerSource("BUS") }, label = { Text("BUS (*TRG)") })
                FilterChip(selected = s.trigSrc.startsWith("IMM"), onClick = { vm.triggerSource("IMM") }, label = { Text("IMEDIATO") })
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                NumberField("V trig", s.vtrig, "%.3f", Modifier.weight(1f)) { vm.triggerLevels(it, null) }
                NumberField("A trig", s.itrig, "%.4f", Modifier.weight(1f)) { vm.triggerLevels(null, it) }
                NumberField("Atraso s", s.trigDel, "%.0f", Modifier.weight(1f)) { vm.triggerDelay(it) }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.padding(top = 6.dp)) {
                Button(onClick = vm::initiate) { Text("INIT") }
                Button(onClick = vm::trigger) { Text("*TRG") }
            }
            Text("Arme com INIT; com fonte BUS, *TRG aplica V/A trig após o atraso.",
                fontSize = 12.sp, color = TextMuted)
        }
        Section("Sistema") {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Travar painel frontal (RWL)", Modifier.weight(1f))
                Switch(checked = lock, onCheckedChange = { lock = it; vm.remote(it) })
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(onClick = vm::local) { Text("Modo LOCAL") }
                OutlinedButton(onClick = { vm.remote(lock) }) { Text("Modo REMOTO") }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(onClick = vm::readErrors) { Text("Ler erros") }
                Button(onClick = { confirmReset = true },
                    colors = ButtonDefaults.buttonColors(containerColor = AlarmColor)) { Text("*RST") }
            }
            HorizontalDivider(Modifier.padding(vertical = 8.dp))
            Text("Fonte: ${ui.idn ?: "-"}", fontSize = 12.sp, color = TextMuted)
            Text("Firmware da ponte: ${ui.fw ?: "-"}", fontSize = 12.sp, color = TextMuted)
            Text("App: $appVer", fontSize = 12.sp, color = TextMuted)
        }
    }
    if (confirmReset) {
        AlertDialog(
            onDismissRequest = { confirmReset = false },
            title = { Text("Resetar a fonte?") },
            text = { Text("*RST desliga a saída e volta todos os ajustes ao padrão de fábrica.") },
            confirmButton = { TextButton(onClick = { vm.reset(); confirmReset = false }) { Text("Resetar") } },
            dismissButton = { TextButton(onClick = { confirmReset = false }) { Text("Cancelar") } },
        )
    }
}

// ---------------------------------------------------------------- Console
@Composable
private fun ConsoleTab(vm: PsuViewModel, ui: UiState) {
    var q by rememberSaveable { mutableStateOf("") }
    val list = rememberLazyListState()
    LaunchedEffect(ui.console.size) { if (ui.console.isNotEmpty()) list.animateScrollToItem(ui.console.size - 1) }
    val quick = listOf("*IDN?", "MEAS:VOLT?", "MEAS:CURR?", "SYST:ERR?", "APPL?", "SYST:VERS?")
    Column(Modifier.fillMaxSize().padding(12.dp)) {
        Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            for (c in quick.take(3)) OutlinedButton(onClick = { vm.scpi(c) },
                contentPadding = PaddingValues(horizontal = 6.dp)) { Text(c, fontSize = 11.sp) }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            for (c in quick.drop(3)) OutlinedButton(onClick = { vm.scpi(c) },
                contentPadding = PaddingValues(horizontal = 6.dp)) { Text(c, fontSize = 11.sp) }
        }
        LazyColumn(
            state = list,
            modifier = Modifier.weight(1f).fillMaxWidth().padding(vertical = 8.dp)
                .background(Lcd, RoundedCornerShape(10.dp)).border(1.dp, Line, RoundedCornerShape(10.dp))
                .padding(10.dp),
        ) {
            items(ui.console) { l ->
                Text(l, fontFamily = FontFamily.Monospace, fontSize = 12.sp,
                    color = when {
                        l.contains("  !") -> AlarmColor
                        l.contains("  <") -> OnColor
                        else -> Color(0xFFB8C2CC)
                    })
            }
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            OutlinedTextField(q, { q = it }, label = { Text("Comando SCPI") }, singleLine = true, shape = RoundedCornerShape(10.dp),
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Send),
                keyboardActions = KeyboardActions(onSend = { if (q.isNotBlank()) { vm.scpi(q); q = "" } }),
                modifier = Modifier.weight(1f))
            Spacer(Modifier.width(8.dp))
            Box(
                Modifier.size(48.dp).background(MaterialTheme.colorScheme.primary, CircleShape)
                    .clickable { if (q.isNotBlank()) { vm.scpi(q); q = "" } },
                contentAlignment = Alignment.Center,
            ) { Text(">", color = MaterialTheme.colorScheme.onPrimary, fontWeight = FontWeight.Bold) }
        }
    }
}
