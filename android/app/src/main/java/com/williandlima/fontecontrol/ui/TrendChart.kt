package com.williandlima.fontecontrol.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.dp
import com.williandlima.fontecontrol.model.Measurement

/**
 * Grafico de tendencia V (eixo esquerdo) e I (eixo direito), cada serie com
 * sua propria escala automatica.
 */
@Composable
fun TrendChart(data: List<Measurement>, modifier: Modifier = Modifier, height: Int = 220) {
    val grid = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.12f)
    Box(modifier.fillMaxWidth().height(height.dp)) {
        if (data.size < 2) {
            Text("Aguardando leituras...", Modifier.align(Alignment.Center),
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.6f))
        } else {
            Series(data, grid)
        }
    }
}

@Composable
private fun BoxScope.Series(data: List<Measurement>, grid: Color) {
    val vMin = data.minOf { it.v }
    val vMax = data.maxOf { it.v }
    val iMin = data.minOf { it.i }
    val iMax = data.maxOf { it.i }
    Canvas(Modifier.matchParentSize().padding(vertical = 18.dp)) {
        for (k in 0..4) {
            val y = size.height * k / 4f
            drawLine(grid, Offset(0f, y), Offset(size.width, y), 1f)
        }
        fun series(sel: (Measurement) -> Double, lo: Double, hi: Double, c: Color) {
            val span = (hi - lo).takeIf { it > 1e-9 } ?: 1.0
            val t0 = data.first().tMs
            val tSpan = (data.last().tMs - t0).coerceAtLeast(1).toFloat()
            val path = Path()
            data.forEachIndexed { idx, m ->
                val x = (m.tMs - t0) / tSpan * size.width
                val y = size.height - ((sel(m) - lo) / span).toFloat() * size.height
                if (idx == 0) path.moveTo(x, y) else path.lineTo(x, y)
            }
            drawPath(path, c, style = Stroke(width = 3f))
        }
        series({ it.v }, vMin, vMax, VoltColor)
        series({ it.i }, iMin, iMax, AmpColor)
    }
    val st = MaterialTheme.typography.labelSmall
    Text("%.3f V".format(vMax), Modifier.align(Alignment.TopStart), color = VoltColor, style = st)
    Text("%.3f V".format(vMin), Modifier.align(Alignment.BottomStart), color = VoltColor, style = st)
    Text("%.4f A".format(iMax), Modifier.align(Alignment.TopEnd), color = AmpColor, style = st)
    Text("%.4f A".format(iMin), Modifier.align(Alignment.BottomEnd), color = AmpColor, style = st)
}
