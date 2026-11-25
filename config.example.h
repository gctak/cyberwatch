// Copie este arquivo como "config.h" (obrigatório, mesmo sem usar o monitor)
// Monitor de servidor: uma segunda página que verifica se o seu servidor está online
#pragma once

// true = ativa a página do monitor | false = só o relógio
#define SERVER_MONITOR_ENABLED false

// Nome exibido na tela e endereço que será consultado
const char* SERVER_NAME = "nome_do_servidor";
const char* SERVER_URL = "https://seu-servidor.com.br";