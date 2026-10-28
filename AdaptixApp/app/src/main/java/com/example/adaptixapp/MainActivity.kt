package com.example.adaptixapp

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.viewmodel.compose.viewModel
import com.example.adaptixapp.ui.c2.C2ConnectScreen
import com.example.adaptixapp.ui.c2.C2DashboardScreen
import com.example.adaptixapp.ui.c2.C2MainViewModel
import com.example.adaptixapp.ui.c2.ConnectionStatus
import com.example.adaptixapp.ui.theme.AdaptixAppTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            AdaptixAppTheme(darkTheme = true) {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background
                ) {
                    val c2ViewModel: C2MainViewModel = viewModel()
                    val uiState by c2ViewModel.uiState.collectAsState()
                    val context = LocalContext.current

                    // 应用启动时自动拉取上次保存的服务器配置
                    LaunchedEffect(Unit) {
                        c2ViewModel.loadSavedConfig(context)
                    }

                    if (uiState.connectionStatus == ConnectionStatus.CONNECTED) {
                        C2DashboardScreen(
                            viewModel = c2ViewModel,
                            onDisconnectClick = {
                                c2ViewModel.disconnect()
                            }
                        )
                    } else {
                        C2ConnectScreen(
                            currentConfig = uiState.serverConfig,
                            connectionStatus = uiState.connectionStatus,
                            errorMessage = uiState.errorMessage,
                            onConnectClick = { config ->
                                c2ViewModel.connectServer(context, config)
                            }
                        )
                    }
                }
            }
        }
    }
}
