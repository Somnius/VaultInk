package com.lefteros.vaultink

import androidx.activity.compose.BackHandler
import androidx.compose.animation.core.tween
import androidx.compose.foundation.clickable
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Shapes
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

// Navy ink on paper, like the reader's own pages. Every surface tone is set, so no Material
// default (a purple tint) shows in menus, dialogs or text fields.
internal val InkLight = lightColorScheme(
    primary = Color(0xFF1F3A5F), onPrimary = Color(0xFFFFFFFF),
    primaryContainer = Color(0xFFD6E2F0), onPrimaryContainer = Color(0xFF0B1E33),
    secondary = Color(0xFF4A5B70), onSecondary = Color(0xFFFFFFFF),
    secondaryContainer = Color(0xFFE2E6EA), onSecondaryContainer = Color(0xFF1B2733),
    tertiary = Color(0xFF6B5A3C), onTertiary = Color(0xFFFFFFFF),
    tertiaryContainer = Color(0xFFEDE3D0), onTertiaryContainer = Color(0xFF2A2110),
    background = Color(0xFFF3F1EC), onBackground = Color(0xFF1B1F24),
    surface = Color(0xFFFBFAF7), onSurface = Color(0xFF1B1F24),
    surfaceVariant = Color(0xFFE9E5DC), onSurfaceVariant = Color(0xFF54575B),
    surfaceContainerLowest = Color(0xFFFFFFFF), surfaceContainerLow = Color(0xFFFBFAF7),
    surfaceContainer = Color(0xFFF7F5F0), surfaceContainerHigh = Color(0xFFEFECE5),
    surfaceContainerHighest = Color(0xFFE9E5DC),
    outline = Color(0xFF8B8A84), outlineVariant = Color(0xFFDAD6CC),
    error = Color(0xFFA3312B), onError = Color(0xFFFFFFFF),
    errorContainer = Color(0xFFF6DDD9), onErrorContainer = Color(0xFF410E0B),
)

// Warm charcoal, not black: the paper turned down, with the navy lifted so it still reads.
internal val InkDark = darkColorScheme(
    primary = Color(0xFFA9C7EA), onPrimary = Color(0xFF0B1E33),
    primaryContainer = Color(0xFF1F3A5F), onPrimaryContainer = Color(0xFFD6E2F0),
    secondary = Color(0xFFB4C3D4), onSecondary = Color(0xFF1B2733),
    secondaryContainer = Color(0xFF34393F), onSecondaryContainer = Color(0xFFDDE3EA),
    tertiary = Color(0xFFD6C4A1), onTertiary = Color(0xFF2A2110),
    tertiaryContainer = Color(0xFF453A26), onTertiaryContainer = Color(0xFFEDE3D0),
    background = Color(0xFF1C1A17), onBackground = Color(0xFFECE6DC),
    surface = Color(0xFF24211D), onSurface = Color(0xFFECE6DC),
    surfaceVariant = Color(0xFF302C26), onSurfaceVariant = Color(0xFFB8AFA2),
    surfaceContainerLowest = Color(0xFF171512), surfaceContainerLow = Color(0xFF211E1A),
    surfaceContainer = Color(0xFF26231F), surfaceContainerHigh = Color(0xFF2E2A25),
    surfaceContainerHighest = Color(0xFF38342E),
    outline = Color(0xFF6E665B), outlineVariant = Color(0xFF3C372F),
    error = Color(0xFFF0A59E), onError = Color(0xFF410E0B),
    errorContainer = Color(0xFF6B1F1A), onErrorContainer = Color(0xFFF6DDD9),
)

/** Literata (OFL-1.1), the reader's book face: titles only. Everything else is the phone's sans. */
internal val Literata = FontFamily(Font(R.font.literata_regular, FontWeight.Normal))

private fun title(size: Int, line: Int) =
    TextStyle(fontFamily = Literata, fontWeight = FontWeight.Normal, fontSize = size.sp, lineHeight = line.sp)

private fun sans(size: Int, line: Int, weight: FontWeight = FontWeight.Normal, spacing: Double = 0.0) =
    TextStyle(fontFamily = FontFamily.Default, fontWeight = weight, fontSize = size.sp, lineHeight = line.sp,
        letterSpacing = spacing.sp)

// The type scale: Literata from display down to titleLarge, sans below. Sizes are in sp, so they
// follow the phone's font size.
internal val InkType = Typography(
    displayLarge = title(44, 52), displayMedium = title(38, 46), displaySmall = title(32, 40),
    headlineLarge = title(30, 38), headlineMedium = title(26, 34), headlineSmall = title(22, 30),
    titleLarge = title(20, 28),
    titleMedium = sans(16, 24, FontWeight.Medium, 0.1),
    titleSmall = sans(14, 20, FontWeight.Medium, 0.1),
    bodyLarge = sans(16, 24, spacing = 0.2),
    bodyMedium = sans(14, 20, spacing = 0.2),
    bodySmall = sans(12, 17, spacing = 0.3),
    labelLarge = sans(14, 20, FontWeight.Medium, 0.1),
    labelMedium = sans(12, 16, FontWeight.Medium, 0.6),
    labelSmall = sans(11, 16, FontWeight.Medium, 0.6),
)

private val InkShapes = Shapes(
    extraSmall = RoundedCornerShape(6.dp), small = RoundedCornerShape(10.dp), medium = RoundedCornerShape(14.dp),
    large = RoundedCornerShape(18.dp), extraLarge = RoundedCornerShape(24.dp),
)

/** Figures of one width, so a number that changes (signal, uptime, a count) doesn't shift its line. */
internal const val TABULAR = "tnum"

/** The only motion in the app: a 150 ms fade. */
internal fun <T> fade() = tween<T>(durationMillis = 150)

@Composable
fun VaultInkTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = if (isSystemInDarkTheme()) InkDark else InkLight, typography = InkType,
        shapes = InkShapes, content = content)
}

/** The small navy label over a group of rows. */
@Composable
fun SectionLabel(text: String, modifier: Modifier = Modifier) {
    Text(text, modifier.padding(start = 4.dp, top = 12.dp), style = MaterialTheme.typography.labelMedium,
        color = MaterialTheme.colorScheme.primary)
}

/** A group of rows on one surface; put [Hairline] between the rows. */
@Composable
fun Group(modifier: Modifier = Modifier, content: @Composable ColumnScope.() -> Unit) {
    Surface(modifier.fillMaxWidth(), shape = MaterialTheme.shapes.medium, color = MaterialTheme.colorScheme.surface,
        border = androidx.compose.foundation.BorderStroke(0.5.dp, MaterialTheme.colorScheme.outlineVariant)) {
        Column(content = content)
    }
}

@Composable
fun Hairline() {
    HorizontalDivider(Modifier.padding(start = 16.dp), thickness = 0.5.dp, color = MaterialTheme.colorScheme.outlineVariant)
}

/** One row of a [Group]: a title, an optional second line, and something at the end. */
@Composable
fun GroupRow(title: String, subtitle: String? = null, onClick: (() -> Unit)? = null,
             trailing: @Composable RowScope.() -> Unit = {}) {
    Row((if (onClick != null) Modifier.clickable(onClick = onClick) else Modifier).fillMaxWidth()
        .heightIn(min = 52.dp).padding(horizontal = 16.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        Column(Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.bodyLarge)
            if (!subtitle.isNullOrEmpty()) {
                Text(subtitle, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
        trailing()
    }
}

/** A label on the left and its value on the right, in tabular figures. */
@Composable
fun ValueRow(label: String, value: String) {
    Row(Modifier.fillMaxWidth().heightIn(min = 44.dp).padding(horizontal = 16.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(16.dp)) {
        Text(label, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(value, Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium.copy(fontFeatureSettings = TABULAR),
            textAlign = androidx.compose.ui.text.style.TextAlign.End)
    }
}

/** The arrow at the end of a row that opens another screen. */
@Composable
fun Chevron() {
    Text("›", style = MaterialTheme.typography.titleLarge, color = MaterialTheme.colorScheme.onSurfaceVariant)
}

/** A full screen over the tabs: Back, a Literata title, then the content in a scrolling column. */
@Composable
fun SubPage(title: String, onBack: () -> Unit, content: @Composable ColumnScope.() -> Unit) {
    BackHandler(onBack = onBack)
    Column(Modifier.fillMaxSize()) {
        Row(Modifier.fillMaxWidth().padding(start = 4.dp, end = 16.dp, top = 4.dp), verticalAlignment = Alignment.CenterVertically) {
            TextButton(onClick = onBack) { Text("‹ " + stringResource(R.string.back)) }
        }
        Text(title, Modifier.padding(horizontal = 20.dp), style = MaterialTheme.typography.headlineMedium,
            maxLines = 2, overflow = TextOverflow.Ellipsis)
        Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp), content = content)
    }
}

/**
 * A password or passphrase: dots unless Show is tapped, no suggestions, and held in plain
 * `remember` by its caller, so it is gone when the screen closes and is never written to the phone.
 */
@Composable
fun SecretField(value: String, onChange: (String) -> Unit, label: String, supporting: String? = null) {
    var shown by remember { mutableStateOf(false) }
    OutlinedTextField(value, onChange, label = { Text(label) }, singleLine = true, modifier = Modifier.fillMaxWidth(),
        visualTransformation = if (shown) VisualTransformation.None else PasswordVisualTransformation(),
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password, autoCorrectEnabled = false),
        supportingText = supporting?.let { { Text(it) } },
        trailingIcon = {
            TextButton(onClick = { shown = !shown }) {
                Text(stringResource(if (shown) R.string.secret_hide else R.string.secret_show))
            }
        })
}
