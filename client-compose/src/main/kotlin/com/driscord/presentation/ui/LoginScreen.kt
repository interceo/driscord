package com.driscord.presentation.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.driscord.driscord_compose.generated.resources.*
import com.driscord.presentation.AppIntent
import com.driscord.presentation.AppUiState
import com.driscord.presentation.AuthStatus
import com.driscord.ui.*
import org.jetbrains.compose.resources.stringResource

@Composable
fun LoginScreen(state: AppUiState, onIntent: (AppIntent) -> Unit) {
    var tab by remember { mutableStateOf(0) } // 0 = login, 1 = register

    Box(
        modifier = Modifier.fillMaxSize().background(ContentBg),
        contentAlignment = Alignment.Center,
    ) {
        Column(
            modifier = Modifier
                .width(360.dp)
                .clip(RoundedCornerShape(8.dp))
                .background(SidebarBg)
                .padding(32.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text(
                "DRISCORD",
                color = TextPrimary,
                fontSize = 20.sp,
                fontWeight = FontWeight.Bold,
                letterSpacing = 1.sp,
            )
            Spacer(Modifier.height(24.dp))

            // Tab row
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(4.dp))
                    .background(BottomBg),
            ) {
                TabButton(
                    text = stringResource(Res.string.login),
                    selected = tab == 0,
                    modifier = Modifier.weight(1f),
                    onClick = { tab = 0 },
                )
                TabButton(
                    text = stringResource(Res.string.register),
                    selected = tab == 1,
                    modifier = Modifier.weight(1f),
                    onClick = { tab = 1 },
                )
            }

            Spacer(Modifier.height(20.dp))

            if (tab == 0) {
                LoginForm(state = state, onIntent = onIntent)
            } else {
                RegisterForm(state = state, onIntent = onIntent)
            }
        }
    }
}

@Composable
private fun LoginForm(state: AppUiState, onIntent: (AppIntent) -> Unit) {
    var username by remember { mutableStateOf("") }
    var password by remember { mutableStateOf("") }

    AuthField(
        label = stringResource(Res.string.username),
        value = username,
        onValueChange = { username = it },
    )
    Spacer(Modifier.height(12.dp))
    AuthField(
        label = stringResource(Res.string.password),
        value = password,
        onValueChange = { password = it },
        isPassword = true,
    )

    ApiErrorText(state.apiError, onIntent)

    Spacer(Modifier.height(16.dp))
    AuthButton(
        text = stringResource(Res.string.login),
        loading = state.authStatus == AuthStatus.LoggingIn,
        enabled = username.isNotBlank() && password.isNotBlank(),
        onClick = { onIntent(AppIntent.Login(username, password)) },
    )
}

@Composable
private fun RegisterForm(state: AppUiState, onIntent: (AppIntent) -> Unit) {
    var username by remember { mutableStateOf("") }
    var email by remember { mutableStateOf("") }
    var password by remember { mutableStateOf("") }

    AuthField(
        label = stringResource(Res.string.username),
        value = username,
        onValueChange = { username = it },
    )
    Spacer(Modifier.height(12.dp))
    AuthField(
        label = stringResource(Res.string.email),
        value = email,
        onValueChange = { email = it },
        keyboardType = KeyboardType.Email,
    )
    Spacer(Modifier.height(12.dp))
    AuthField(
        label = stringResource(Res.string.password),
        value = password,
        onValueChange = { password = it },
        isPassword = true,
    )

    ApiErrorText(state.apiError, onIntent)

    Spacer(Modifier.height(16.dp))
    AuthButton(
        text = stringResource(Res.string.register),
        loading = state.authStatus == AuthStatus.LoggingIn,
        enabled = username.isNotBlank() && email.isNotBlank() && password.isNotBlank(),
        onClick = { onIntent(AppIntent.Register(username, email, password)) },
    )
}

// ---------------------------------------------------------------------------
// Shared sub-components
// ---------------------------------------------------------------------------

@Composable
private fun TabButton(text: String, selected: Boolean, modifier: Modifier, onClick: () -> Unit) {
    Box(
        modifier = modifier
            .clip(RoundedCornerShape(4.dp))
            .background(if (selected) Blurple else Color.Transparent)
            .clickable(onClick = onClick)
            .padding(vertical = 8.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(text, color = if (selected) Color.White else TextMuted, fontSize = 13.sp, fontWeight = FontWeight.Medium)
    }
}

@Composable
private fun AuthField(
    label: String,
    value: String,
    onValueChange: (String) -> Unit,
    isPassword: Boolean = false,
    keyboardType: KeyboardType = KeyboardType.Text,
) {
    Column(modifier = Modifier.fillMaxWidth()) {
        Text(label, color = TextMuted, fontSize = 11.sp, letterSpacing = 0.3.sp)
        Spacer(Modifier.height(4.dp))
        OutlinedTextField(
            value = value,
            onValueChange = onValueChange,
            singleLine = true,
            modifier = Modifier.fillMaxWidth(),
            visualTransformation = if (isPassword) PasswordVisualTransformation() else androidx.compose.ui.text.input.VisualTransformation.None,
            keyboardOptions = KeyboardOptions(keyboardType = if (isPassword) KeyboardType.Password else keyboardType),
            colors = TextFieldDefaults.outlinedTextFieldColors(
                textColor = TextPrimary,
                unfocusedBorderColor = FieldBg,
                focusedBorderColor = Blurple,
                backgroundColor = BottomBg,
                cursorColor = Blurple,
            ),
            textStyle = LocalTextStyle.current.copy(fontSize = 13.sp),
        )
    }
}

@Composable
private fun AuthButton(text: String, loading: Boolean, enabled: Boolean, onClick: () -> Unit) {
    Button(
        onClick = onClick,
        enabled = enabled && !loading,
        modifier = Modifier.fillMaxWidth().height(38.dp),
        colors = ButtonDefaults.buttonColors(backgroundColor = Blurple),
        shape = RoundedCornerShape(4.dp),
        contentPadding = PaddingValues(0.dp),
    ) {
        if (loading) {
            CircularProgressIndicator(modifier = Modifier.size(18.dp), strokeWidth = 2.dp, color = Color.White)
        } else {
            Text(text, color = Color.White, fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
        }
    }
}

@Composable
private fun ApiErrorText(error: String?, onIntent: (AppIntent) -> Unit) {
    if (error != null) {
        Spacer(Modifier.height(10.dp))
        Text(
            text = error,
            color = Red,
            fontSize = 12.sp,
            textAlign = TextAlign.Center,
            modifier = Modifier.fillMaxWidth().clickable { onIntent(AppIntent.DismissApiError) },
        )
    }
}
