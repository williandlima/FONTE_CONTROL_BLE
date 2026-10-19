package com.williandlima.fontecontrol.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp

// Paleta escura de instrumento de bancada
val Bg = Color(0xFF0A0D10)          // fundo da tela
val Panel = Color(0xFF12171C)       // cartões e barra superior
val PanelHigh = Color(0xFF1A2027)   // diálogos e itens elevados
val Line = Color(0xFF242C35)        // bordas e divisores
val Lcd = Color(0xFF06090C)         // fundo do visor e do console
val TextMain = Color(0xFFE6EBF0)
val TextMuted = Color(0xFF8A96A3)
val Accent = Color(0xFF3FB6A8)      // destaque (abas, chaves)

val VoltColor = Color(0xFF5BC0EB)
val AmpColor = Color(0xFFF2A93B)
val OnColor = Color(0xFF3DDC84)
val AlarmColor = Color(0xFFE5484D)
val OnButton = Color(0xFF1E7B4A)    // botão "ligar saída"
val AlarmButton = Color(0xFFB3363A) // botão "desligar saída"

private val Dark = darkColorScheme(
    primary = Accent,
    onPrimary = Color(0xFF00201C),
    secondary = VoltColor,
    onSecondary = Color(0xFF00212E),
    secondaryContainer = Color(0xFF17323C),
    onSecondaryContainer = Color(0xFFBFEAF8),
    tertiary = AmpColor,
    background = Bg,
    onBackground = TextMain,
    surface = Panel,
    onSurface = TextMain,
    surfaceVariant = PanelHigh,
    onSurfaceVariant = TextMuted,
    surfaceContainerLowest = Bg,
    surfaceContainerLow = Panel,
    surfaceContainer = Panel,
    surfaceContainerHigh = PanelHigh,
    surfaceContainerHighest = PanelHigh,
    outline = Color(0xFF3A4551),
    outlineVariant = Line,
    error = AlarmColor,
    onError = Color.White,
)

private val AppShapes = Shapes(
    small = RoundedCornerShape(8.dp),
    medium = RoundedCornerShape(12.dp),
    large = RoundedCornerShape(16.dp),
)

/** Tema sempre escuro, independente do tema do sistema. */
@Composable
fun FonteTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = Dark, shapes = AppShapes, content = content)
}

/** Cartão padrão: fundo de painel com borda fina. */
@Composable
fun PanelCard(modifier: Modifier = Modifier, content: @Composable ColumnScope.() -> Unit) {
    Card(
        modifier = modifier,
        shape = RoundedCornerShape(12.dp),
        colors = CardDefaults.cardColors(containerColor = Panel),
        border = BorderStroke(1.dp, Line),
        content = content,
    )
}
