package com.williandlima.fontecontrol.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

val VoltColor = Color(0xFF4FC3F7)
val AmpColor = Color(0xFFFFB74D)
val OnColor = Color(0xFF43A047)
val AlarmColor = Color(0xFFE53935)

private val Dark = darkColorScheme(
    primary = Color(0xFF7CFC9A),
    onPrimary = Color(0xFF00391A),
    secondary = VoltColor,
    background = Color(0xFF101418),
    surface = Color(0xFF161B20),
    surfaceVariant = Color(0xFF222A31),
)

private val Light = lightColorScheme(
    primary = Color(0xFF1B7A3A),
    secondary = Color(0xFF0277BD),
)

@Composable
fun FonteTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = if (isSystemInDarkTheme()) Dark else Light, content = content)
}
