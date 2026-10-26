/* ------------------------------------------------------------------
   TCC - Monitor de qualidade do ar em ambientes de ensino
   Mateus Pinheiro Alcantara - Engenharia Eletronica, POLI/UPE

   Firmware v15-CAMPO - preparado em 03/10/2026 para a I09 (05 a 08/10)

   VERSAO DE CAMPO SEM REDE INSTITUCIONAL (diario, secao 4.90).
   As credenciais da POLI-SMART nao chegaram. O envio usa o ROTEAMENTO
   DO CELULAR do operador, ligado so quando ele esta perto do aparelho:

     - na instalacao: primeira passagem 3 MIN apos ligar. Com o
       roteamento ligado, a pagina confirma em minutos que a sala esta
       gravando, e a linha de ANCORA fixa o relogio desta sala.
     - nas outras horas: a passagem a cada 6 h tenta, nao acha a rede
       em ~20 s, grava "# ENVIO ABORTADO - sem rede" e desiste. Nada se
       perde - o ponteiro so avanca com confirmacao do servidor.
     - em casa, depois da retirada: ligar no carregador com o
       roteamento ligado; a passagem de 3 min sobe a semana inteira.

   Unica diferenca para o v15 de bancada: LOCAL, rede e
   MIN_PRIMEIRA_PASSAGEM (15 -> 3). O resto e identico ao verificado
   em 02/10 (diario 4.81 a 4.83).

   --- texto do v15 de bancada, mantido abaixo ---
   Firmware v15 - 02/10/2026

   O QUE MUDOU NA v15, e por que (diario, secoes 4.71 a 4.78):

     1. MOTIVO DO REINICIO gravado na marca "# BOOT" do log. Em 01/10 o
        aparelho reiniciou quatro vezes e travou por 8,5 h sem que
        ninguem soubesse por que. Agora cada boot diz se foi queda de
        tensao (brownout), watchdog, erro de execucao ou religamento.

     2. WATCHDOG de 120 s. Se o laco principal parar, o aparelho
        reinicia sozinho e volta a gravar. Em 01/10 um travamento durou
        ate alguem ir a bancada; em campo isso seria uma sala perdida.

     3. PRIMEIRA PASSAGEM 15 MIN APOS O BOOT, depois a cada 6 h. A pagina
        de situacao confirma no mesmo dia que a sala esta gravando - e
        um reinicio aparece na pagina em 15 min, nao em 6 h.

     4. K_VCC RECALIBRADO para 5,713 (provisorio, secao 4.78). O valor
        5,4704 fora calibrado em 17/09 com 66 mV de queda no terra da
        protoboard embutidos; corrigido o terra em 01/10, o vcc passou a
        ser lido ~0,21 V abaixo do real.

   Rede: roteador de casa, SSID da rede de casa, enquanto estiver na bancada.
   Para o campo: trocar LOCAL, a rede (POLI-SMART) e rodar Z antes.

   v11: comando S (varredura de redes) e aviso de LOTE de teste.
   LOTE em 5, valor de TESTE - ver aviso no boot.

   v10 - 28/09: ENVIO EM LOTE PARA A NUVEM, com ponteiro de retomada.

   ---------------------------------------------------------------
   PRINCIPIO DE PROJETO, e nao e negociavel:

     A FLASH E O REGISTRO DE REFERENCIA. A NUVEM E CANAL PARALELO.

   Medido nesta campanha (secao 4.14 do diario):
     serial ... 14,8 % de perda
     flash .... 0,000 % em 96,2 h

   Por isso, e ao contrario do que o pre-projeto previa:
     - o rADIO FICA DESLIGADO durante toda a coleta;
     - a flash NUNCA e apagada automaticamente;
     - o ponteiro de envio so avanca quando o SERVIDOR CONFIRMA
       quantas linhas gravou, e o numero bate com o que foi enviado.

   NAO EXISTE APAGAMENTO AUTOMATICO. Foi pedido e foi recusado
   (secao 4.26 do diario). Um Apps Script pode devolver 200 e expirar
   antes de escrever; a planilha pode bater limite de celulas; a cota
   pode esgotar. Em todos esses casos o ESP32 ve sucesso. Apagar a
   flash com base nesse "sucesso" custaria um ambiente inteiro, e sao
   tres ambientes em seis semanas, sem janela para repeticao.
   O apagamento continua manual, por Z, depois de voce conferir a
   contagem de linhas na planilha contra a do arquivo.
   ---------------------------------------------------------------

   POR QUE O RADIO FICA DESLIGADO DURANTE A MEDIDA (secao 4.25):
   o radio puxa rajadas de 200 a 300 mA contra os ~130 mA do aquecedor
   em regime. Essa rajada cai sobre o diodo Schottky do DevKit e os
   contatos da protoboard, e afunda o VCC - que esta dentro de
   Rs = RL*(VCC - V)/V. A medida e imune a afundamento PROPORCIONAL,
   porque o fator se cancela na razao; o que quebra a imunidade e o
   DESCASAMENTO TEMPORAL, ja que lerVCC() amostra uma vez, ~0,2 s
   antes de uma janela de 30 s. Com o radio desligado o problema nao
   existe, em vez de ser pequeno.

   NTP: a janela de envio e o unico momento com rede, e e onde se
   ancora o contador de minutos ao relogio de parede. Sem isso nao ha
   como correlacionar o indice com a GRADE DE HORARIOS - que o
   pre-projeto (secao 4) chama de "o resultado mais importante".
   A ancora e gravada no proprio log como linha de comentario.

   Comandos pela serial:
     I -> informacoes do sistema de arquivos e do ponteiro de envio
     D -> despeja todo o log pela serial
     Z -> APAGA o log (manual, e continua sendo a unica forma)
     E -> forca uma passagem de envio agora
     W -> testa a conexao de rede sem enviar nada
     R -> zera o ponteiro SEM apagar o log (reenvia tudo de proposito)
     S -> varre as redes ao alcance e mostra nome exato, sinal e seguranca
   ------------------------------------------------------------------ */

#include <math.h>
#include <string.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>   // ESP_ARDUINO_VERSION_MAJOR
#include <esp_system.h>      // esp_reset_reason()
#include <esp_task_wdt.h>    // watchdog de tarefa

/* ================= CONFIGURACAO - EDITE ESTA SECAO =================

   FALTA UMA COISA SO: a senha da rede de casa, logo abaixo.

   Confira so duas coisas antes de subir:
     1. a senha da rede de casa esta preenchida abaixo
     2. o aparelho esta no CARREGADOR DE PAREDE, nunca no USB do PC
     3. a bancada nao vai ser tocada ate voce baixar o log

   A funcao configIncompleta(), mais abaixo, avisa no boot se algum
   campo voltar a ficar por preencher. Nao mexa nela.
   =================================================================== */

// Identificacao do ambiente. MUDE A CADA SALA. Vai em toda linha enviada.
// Agora: BANCADA (teste em casa). Depois: "B2", "I9", "K13".
const char *LOCAL = "I09";            // CAMPO: semana de 05 a 08/10

// --- rede ---
// Deixe ENTERPRISE em 0 para rede com senha unica (WPA2-PSK).
// Ponha 1 se o DTI responder que a rede e WPA2-Enterprise (PEAP/MSCHAPv2).
#define ENTERPRISE 0

// Roteador de casa. Confirmado na varredura de 28/09 a -48 dBm, em 2,4 GHz.
// Se o roteador tiver 2,4 e 5 GHz com nomes diferentes, use o de 2,4.
// CAMPO: ROTEAMENTO DO CELULAR. Configure no telefone:
//   - banda 2,4 GHz (o ESP32 NAO enxerga 5 GHz)
//   - seguranca WPA2-Personal (nao "WPA3 somente")
//   - nome SEM espaco e SEM acento, por exemplo TCC-MATEUS
const char *WIFI_SSID  = "<<<< NOME DA REDE >>>>";

// >>>>>>>>>>>>  PREENCHA A SENHA DA REDE AQUI  <<<<<<<<<<<<
// E a UNICA coisa a editar neste arquivo. O firmware avisa no boot
// se voce esquecer, e a gravacao na flash funciona de qualquer jeito.
const char *WIFI_SENHA = "<<<< SENHA DA REDE >>>>";

#if ENTERPRISE
  // Peca ao DTI uma CREDENCIAL DE DISPOSITIVO. Nao use a sua pessoal:
  // o aparelho fica uma semana sem supervisao numa sala de aula, e se
  // for levado a credencial vai junto.
  const char *EAP_IDENTIDADE = "identidade";
  const char *EAP_USUARIO    = "usuario";
  const char *EAP_SENHA      = "<<<< SENHA EAP >>>>";
#endif

// URL do Web App do Google Apps Script (termina em /exec).
// Validada em 28/09 pelo testa_nuvem.py: OK 5 e OK 60.
const char *URL_NUVEM = "COLOQUE AQUI A URL DO WEB APP (termina em /exec)";

// Chave compartilhada: o script recusa qualquer POST sem ela.
// Nao e seguranca forte - e para evitar que um endereco publico seja
// poluido por acidente ou por varredura.
// TEM QUE SER IDENTICA a CHAVE_ESPERADA do nuvem_apps_script.gs.
// Copie e cole de la, nao digite - um caractere diferente e ERRO chave.
const char *CHAVE = "<<<< CHAVE COMPARTILHADA >>>>";

// --- ritmo do envio ---
// 6 h tambem em campo (decidido em 29/09): a pagina mostra o aparelho
// vivo quatro vezes por dia, e cada passagem custa ~1,5 min de radio.
const uint32_t HORAS_ENTRE_ENVIOS = 6;    // passagem automatica a cada N horas
// Primeira passagem pouco depois do boot: confirma na pagina, no mesmo
// dia da instalacao, que o aparelho esta gravando. Em campo cai dentro
// das 6 h iniciais de descarte, entao nao toca em dado valido.
const uint32_t MIN_PRIMEIRA_PASSAGEM = 3;    // CAMPO: 3 min (bancada: 15)
// Watchdog: maior que a pior espera isolada (Wi-Fi 20 s, NTP 8 s,
// HTTP 2 x 15 s por tentativa). E alimentado entre essas esperas.
const uint32_t WDT_S = 120;
// De volta ao valor de operacao. O teste de retomada de 28/09 usou 5,
// de proposito, para caber varios lotes numa passagem curta.
//
// A passagem de 28/09 mediu 5,7 s POR LOTE - custo dominado por dois
// handshakes TLS (um no /exec, outro no googleusercontent do
// redirecionamento). Em campo isso importa: uma semana a 30 s daria
// 336 lotes, ou 32 min de passagem - e durante a passagem a medida
// fica parada. Reduzir esse tempo e assunto em aberto.
const uint16_t LOTE               = 60;   // linhas por requisicao
const uint8_t  TENTATIVAS         = 3;    // por lote, antes de abortar a passagem
const uint32_t TIMEOUT_MS         = 15000;
const uint32_t ESPERA_WIFI_MS     = 20000;

/* =================== FIM DA CONFIGURACAO =================== */

// Cabecalhos do WPA2-Enterprise. Ficam AQUI, e nao no topo do arquivo,
// porque dependem do #define ENTERPRISE que esta na secao acima - o
// pre-processador le de cima para baixo, e la em cima ENTERPRISE ainda
// nao existe.
//
// Entram so quando ENTERPRISE == 1: esp_eap_client.h so existe no
// Arduino-ESP32 3.x e esp_wpa2.h so existe no 2.x, de modo que incluir
// o errado quebra a compilacao mesmo sem usar o recurso.
#if ENTERPRISE
  #if ESP_ARDUINO_VERSION_MAJOR >= 3
    #include <esp_eap_client.h>
  #else
    #include <esp_wpa2.h>
  #endif
#endif

// Aviso em tempo de execucao: se algum campo ficou por preencher, o
// firmware avisa no boot em vez de falhar em silencio na primeira
// passagem de envio.
bool configIncompleta() {
  return strstr(WIFI_SSID, "<<<<") || strstr(WIFI_SENHA, "<<<<") || strstr(CHAVE, "<<<<") ||
         strstr(URL_NUVEM, "COLOQUE");
}

#define PIN_AO   34
#define PIN_DHT   4
#define PIN_VCC  35

const float    VCC_RESERVA = 4.98;
const float    K_VCC     = 5.713;    // v15: provisorio (diario 4.78). Era 5.4704.
const float    VCC_MIN   = 4.00;
const float    VCC_MAX   = 5.50;
const uint16_t N_VCC     = 200;
const float    DIVISOR   = 1.991;
const float    RL        = 135.4;
const float    ADC_A     = 4.3332e-4;
const float    ADC_B     = 0.0199;
const uint32_t INTERVALO = 30000;
const uint16_t LIM_OUT   = 100;

const char *ARQ_LOG   = "/log.csv";
const char *ARQ_BOOT  = "/boot.txt";
const char *ARQ_PONT  = "/envio.txt";     // ponteiro de envio, em bytes
const size_t RESERVA  = 24000;
const char *CABECALHO =
  "boot,min,adc,dp,adc_min,adc_max,n_out,n,V_no,V_sensor,Rs_kohm,t_c,ur_pct,adc_vcc,vcc,estado";

uint32_t nboot = 0;
bool     fs_ok = false;
bool     cheio = false;

uint32_t pos_enviada = 0;       // bytes do log.csv ja confirmados na nuvem
uint32_t prox_envio_ms = 0;     // millis() da proxima passagem automatica
uint32_t total_enviadas = 0;    // linhas confirmadas nesta sessao
bool     hora_ancorada = false;

// Buffer do lote. GLOBAL de proposito: a pilha da tarefa do loop() tem
// 8 KB por padrao no Arduino-ESP32, e um buffer deste tamanho como
// variavel local a estouraria silenciosamente.
static char lote_buf[10240];

/* ---------------- watchdog e motivo do reinicio ---------------- */
// Sem virgulas nos textos: as marcas vao para a planilha, que separa
// colunas por virgula.
const char *motivoReset() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "RELIGAMENTO (energia)";
    case ESP_RST_EXT:       return "pino EN";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "PANIC (erro de execucao)";
    case ESP_RST_INT_WDT:   return "WATCHDOG de interrupcao";
    case ESP_RST_TASK_WDT:  return "WATCHDOG de tarefa (travamento)";
    case ESP_RST_WDT:       return "WATCHDOG (outro)";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_BROWNOUT:  return "BROWNOUT (queda de tensao)";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "desconhecido";
  }
}

// O core 2.x e o 3.x do Arduino-ESP32 tem APIs diferentes para isto.
void iniciarWatchdog() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms     = WDT_S * 1000;
  cfg.idle_core_mask = 0;
  cfg.trigger_panic  = true;
  if (esp_task_wdt_reconfigure(&cfg) != ESP_OK) esp_task_wdt_init(&cfg);
#else
  esp_task_wdt_init(WDT_S, true);
#endif
  esp_task_wdt_add(NULL);          // vigia a tarefa do loop()
}

inline void alimentarWatchdog() { esp_task_wdt_reset(); }

/* ---------------- DHT11 - leitura sem biblioteca ---------------- */
static bool esperaNivel(uint8_t pino, uint8_t nivel, uint32_t limite_us) {
  uint32_t t0 = micros();
  while (digitalRead(pino) != nivel) {
    if (micros() - t0 > limite_us) return false;
  }
  return true;
}

bool lerDHT11(uint8_t pino, float &temp, float &umid) {
  uint8_t d[5] = {0, 0, 0, 0, 0};
  pinMode(pino, OUTPUT);
  digitalWrite(pino, LOW);
  delay(20);
  digitalWrite(pino, HIGH);
  delayMicroseconds(30);
  pinMode(pino, INPUT_PULLUP);
  if (!esperaNivel(pino, LOW,  120)) return false;
  if (!esperaNivel(pino, HIGH, 120)) return false;
  if (!esperaNivel(pino, LOW,  120)) return false;
  for (uint8_t i = 0; i < 40; i++) {
    if (!esperaNivel(pino, HIGH, 120)) return false;
    uint32_t t0 = micros();
    if (!esperaNivel(pino, LOW, 160)) return false;
    uint32_t largura = micros() - t0;
    d[i / 8] <<= 1;
    if (largura > 45) d[i / 8] |= 1;
  }
  if ((uint8_t)(d[0] + d[1] + d[2] + d[3]) != d[4]) return false;
  umid = d[0] + d[1] * 0.1f;
  temp = d[2] + d[3] * 0.1f;
  return true;
}

/* ---------------- automedicao do VCC ---------------- */
float lerVCC(float &adc_bruto) {
  double soma = 0;
  for (uint16_t i = 0; i < N_VCC; i++) {
    soma += analogRead(PIN_VCC);
    delay(1);
  }
  adc_bruto = soma / N_VCC;
  float v = (ADC_A * adc_bruto + ADC_B) * K_VCC;
  return (v >= VCC_MIN && v <= VCC_MAX) ? v : VCC_RESERVA;
}

/* ---------------- arquivo ---------------- */
void gravar(const char *linha) {
  if (!fs_ok || cheio) return;
  size_t livre = LittleFS.totalBytes() - LittleFS.usedBytes();
  if (livre < RESERVA) {
    cheio = true;
    Serial.println("# *** AVISO: FLASH CHEIA - GRAVACAO INTERROMPIDA - serial continua ***");
    return;
  }
  File f = LittleFS.open(ARQ_LOG, FILE_APPEND);
  if (!f) return;
  f.println(linha);
  f.close();
}

uint32_t tamanhoLog() {
  File f = LittleFS.open(ARQ_LOG, FILE_READ);
  if (!f) return 0;
  uint32_t t = f.size();
  f.close();
  return t;
}

/* ---------------- ponteiro de envio ---------------- */
// Guardado em arquivo proprio. Se o aparelho cair no meio de uma
// passagem, ele retoma do ultimo lote CONFIRMADO, nao do inicio.
void lerPonteiro() {
  pos_enviada = 0;
  File f = LittleFS.open(ARQ_PONT, FILE_READ);
  if (f) { pos_enviada = (uint32_t)f.parseInt(); f.close(); }
  uint32_t tam = tamanhoLog();
  if (pos_enviada > tam) {
    // O log encolheu - so acontece depois de um Z. Recomeca do zero.
    Serial.println("# ponteiro maior que o log (apagamento?) - zerado");
    pos_enviada = 0;
  }
}

void gravarPonteiro() {
  File f = LittleFS.open(ARQ_PONT, FILE_WRITE);
  if (f) { f.println(pos_enviada); f.close(); }
}

/* ---------------- rede ---------------- */
bool conectarWiFi() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
#if ENTERPRISE
  #if ESP_ARDUINO_VERSION_MAJOR >= 3
    esp_eap_client_set_identity((uint8_t *)EAP_IDENTIDADE, strlen(EAP_IDENTIDADE));
    esp_eap_client_set_username((uint8_t *)EAP_USUARIO, strlen(EAP_USUARIO));
    esp_eap_client_set_password((uint8_t *)EAP_SENHA, strlen(EAP_SENHA));
    esp_wifi_sta_enterprise_enable();
  #else
    esp_wifi_sta_wpa2_ent_set_identity((uint8_t *)EAP_IDENTIDADE, strlen(EAP_IDENTIDADE));
    esp_wifi_sta_wpa2_ent_set_username((uint8_t *)EAP_USUARIO, strlen(EAP_USUARIO));
    esp_wifi_sta_wpa2_ent_set_password((uint8_t *)EAP_SENHA, strlen(EAP_SENHA));
    esp_wifi_sta_wpa2_ent_enable();
  #endif
  WiFi.begin(WIFI_SSID);
#else
  WiFi.begin(WIFI_SSID, WIFI_SENHA);
#endif

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < ESPERA_WIFI_MS) {
    delay(250);
    alimentarWatchdog();
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("# wifi ok: %s  rssi=%d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
  }
  Serial.println("# wifi FALHOU");
  return false;
}

void desligarWiFi() {
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  delay(50);
}

// Ancora o contador de minutos no relogio de parede. Sem isto nao ha
// correlacao possivel com a grade de horarios.
//
// Roda em TODA passagem, e nao so na primeira, por dois motivos:
//
//  1. Se a flash for apagada depois de uma ancora, a linha de ancora vai
//     junto. Em campo o protocolo manda apagar antes de cada ambiente -
//     e o log daquela sala ficaria SEM referencia de tempo nenhuma.
//
//  2. Varias ancoras ao longo da semana permitem MEDIR a deriva do
//     millis(). O cristal do ESP32 tem tolerancia da ordem de dezenas de
//     ppm, o que da alguns minutos em sete dias - suficiente para
//     deslocar o inicio de uma aula na correlacao com a grade. Com duas
//     ancoras ou mais a deriva deixa de ser suposta e passa a ser
//     corrigivel linha a linha.
void ancorarHora() {
  configTime(-3 * 3600, 0, "pool.ntp.org", "a.st1.ntp.br");   // Recife, UTC-3
  struct tm ti;
  if (!getLocalTime(&ti, 8000)) {
    Serial.println("# NTP falhou - contador de minutos segue sem ancora");
    return;
  }
  char marca[160];
  snprintf(marca, sizeof(marca),
           "# ANCORA %04d-%02d-%02d %02d:%02d:%02d em min=%.1f boot=%lu local=%s",
           ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
           ti.tm_hour, ti.tm_min, ti.tm_sec,
           millis() / 60000.0, (unsigned long)nboot, LOCAL);
  Serial.println(marca);
  gravar(marca);
  hora_ancorada = true;      // so informativo; nao impede nova ancora
}

// Encurta uma resposta longa para o monitor serial. Sem isto, uma
// pagina de erro do Google inunda a tela e esconde as linhas uteis -
// foi o que aconteceu no primeiro teste de envio.
String resumo(const String &r) {
  String t = r;
  t.replace('\n', ' ');
  t.replace('\r', ' ');
  if (t.length() > 110) t = t.substring(0, 110) + "...";
  return t;
}

/* ---------------- envio de um lote ----------------
   Monta ate LOTE linhas a partir de pos_enviada, envia, e SO avanca o
   ponteiro se o servidor confirmar o mesmo numero de linhas.
   Devolve: 1 enviou e confirmou | 0 nada a enviar | -1 falhou  */
int enviarLote() {
  File f = LittleFS.open(ARQ_LOG, FILE_READ);
  if (!f) return -1;
  if (!f.seek(pos_enviada)) { f.close(); return -1; }

  size_t usado = 0;
  uint16_t n_linhas = 0;
  uint32_t pos_fim = pos_enviada;

  while (n_linhas < LOTE && f.available()) {
    String linha = f.readStringUntil('\n');
    // A posicao vem do PROPRIO arquivo, nunca de linha.length()+1: se o
    // log terminar sem \n, a aritmetica manual passa do fim, o ponteiro
    // fica maior que o arquivo, lerPonteiro() o zera e TUDO e reenviado.
    uint32_t pos_depois = f.position();
    linha.trim();

    // Ultima linha ainda sendo escrita: para aqui e deixa para a proxima
    // passagem, quando ela ja estara completa.
    if (!f.available() && linha.length() == 0) break;

    if (linha.length() > 0) {
      if (usado + linha.length() + 2 >= sizeof(lote_buf)) break;
      memcpy(lote_buf + usado, linha.c_str(), linha.length());
      usado += linha.length();
      lote_buf[usado++] = '\n';
      n_linhas++;
    }
    pos_fim = pos_depois;
  }
  lote_buf[usado] = 0;
  f.close();

  if (n_linhas == 0) return 0;

  WiFiClientSecure cli;
  // setInsecure(): nao valida o certificado do Google. Aceitavel aqui
  // porque o conteudo enviado nao e sensivel e a chave compartilhada
  // protege o destino. Declarar como limitacao no relatorio.
  cli.setInsecure();

  // Cabecalho do corpo: uma linha de metadados, depois o CSV cru.
  char meta[160];
  snprintf(meta, sizeof(meta), "%s|%s|%lu|%lu|%u\n",
           CHAVE, LOCAL, (unsigned long)nboot,
           (unsigned long)pos_enviada, n_linhas);

  String corpo;
  corpo.reserve(usado + strlen(meta) + 8);
  corpo = meta;
  corpo += lote_buf;

  /* -------- redirecionamento tratado A MAO --------------------------
     O Apps Script NUNCA responde no /exec. Ele devolve 302 apontando
     para script.googleusercontent.com/macros/echo?user_content_key=...
     e a resposta de verdade so aparece se esse endereco for buscado
     com GET.

     O setFollowRedirects() do HTTPClient reenvia o POST para o destino
     do redirecionamento, e o googleusercontent recusa POST com
     "400 Bad Request" - que foi exatamente o que aconteceu em 28/09.
     O Python nao errou porque o urllib converte POST em GET ao seguir
     um 302, como manda a pratica corrente.

     Aqui o redirecionamento e seguido explicitamente: POST no /exec,
     le o cabecalho Location, GET nele. Sem depender do comportamento
     da biblioteca, que varia entre versoes do core.
     ------------------------------------------------------------------ */
  HTTPClient http;
  http.setTimeout(TIMEOUT_MS);
  http.setConnectTimeout(TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

  const char *cab[] = {"Location"};
  if (!http.begin(cli, URL_NUVEM)) return -1;
  http.collectHeaders(cab, 1);
  http.addHeader("Content-Type", "text/plain");

  int codigo = http.POST(corpo);
  String resp;
  String destino = http.header("Location");
  if (codigo == 200) resp = http.getString();
  http.end();

  if (codigo == 301 || codigo == 302 || codigo == 303 || codigo == 307) {
    if (destino.length() == 0) {
      Serial.printf("# lote FALHOU: http=%d sem cabecalho Location\n", codigo);
      return -1;
    }
    // GET no destino - e aqui que a resposta do script aparece.
    if (!http.begin(cli, destino)) return -1;
    codigo = http.GET();
    resp = http.getString();
    http.end();
  }
  resp.trim();

  if (codigo != 200) {
    Serial.printf("# lote FALHOU: http=%d resp=%s\n", codigo, resumo(resp).c_str());
    return -1;
  }
  // Contrato com o script: "OK <linhas_gravadas>". Qualquer outra coisa
  // e falha, inclusive 200 com corpo vazio - que e exatamente o modo de
  // falha silenciosa que motivou este protocolo.
  if (!resp.startsWith("OK ")) {
    Serial.printf("# lote RECUSADO: resp=%s\n", resumo(resp).c_str());
    return -1;
  }
  int gravadas = resp.substring(3).toInt();
  if (gravadas != n_linhas) {
    Serial.printf("# lote INCOMPLETO: enviadas=%u gravadas=%d - ponteiro NAO avanca\n",
                  n_linhas, gravadas);
    return -1;
  }

  pos_enviada = pos_fim;
  gravarPonteiro();
  total_enviadas += n_linhas;
  return 1;
}

/* ---------------- passagem completa de envio ---------------- */
void passagemEnvio(bool forcada) {
  uint32_t tam = tamanhoLog();
  if (pos_enviada >= tam) {
    Serial.println("# envio: nada novo a enviar");
    prox_envio_ms = millis() + HORAS_ENTRE_ENVIOS * 3600000UL;
    return;
  }

  Serial.printf("# ===== PASSAGEM DE ENVIO (%s) - %lu de %lu bytes pendentes\n",
                forcada ? "forcada" : "automatica",
                (unsigned long)(tam - pos_enviada), (unsigned long)tam);

  // Marca a interrupcao da medida no proprio log. A passagem cria um
  // vazio de alguns minutos na serie, e ele tem que estar declarado.
  char marca[120];
  snprintf(marca, sizeof(marca), "# ENVIO INICIO min=%.1f pendente=%lu bytes",
           millis() / 60000.0, (unsigned long)(tam - pos_enviada));
  gravar(marca);

  if (!conectarWiFi()) {
    desligarWiFi();
    gravar("# ENVIO ABORTADO - sem rede");
    prox_envio_ms = millis() + HORAS_ENTRE_ENVIOS * 3600000UL;
    return;
  }

  ancorarHora();

  uint32_t t0 = millis();
  uint16_t lotes_ok = 0;
  bool abortou = false;

  while (pos_enviada < tamanhoLog()) {
    int r = -1;
    for (uint8_t tent = 0; tent < TENTATIVAS && r < 0; tent++) {
      alimentarWatchdog();
      if (tent) {
        Serial.printf("# repetindo lote (tentativa %u)\n", tent + 1);
        delay(2000UL * tent);              // recuo progressivo
      }
      r = enviarLote();
    }
    if (r < 0) { abortou = true; break; }
    if (r == 0) break;                      // nada mais a enviar
    lotes_ok++;
    if (lotes_ok % 10 == 0)
      Serial.printf("# %u lotes, %lu linhas confirmadas\n",
                    lotes_ok, (unsigned long)total_enviadas);
    delay(150);                             // folga para o Apps Script
  }

  desligarWiFi();

  snprintf(marca, sizeof(marca),
           "# ENVIO FIM min=%.1f lotes=%u linhas=%lu ponteiro=%lu %s",
           millis() / 60000.0, lotes_ok, (unsigned long)total_enviadas,
           (unsigned long)pos_enviada, abortou ? "ABORTADO" : "ok");
  gravar(marca);
  Serial.println(marca);
  Serial.printf("# passagem levou %.1f s. Radio desligado, medida retomada.\n",
                (millis() - t0) / 1000.0);

  if (abortou)
    Serial.println("# ATENCAO: passagem abortada. O ponteiro NAO avancou no lote que falhou. "
                   "Nada foi perdido - a proxima passagem retoma dali.");

  prox_envio_ms = millis() + HORAS_ENTRE_ENVIOS * 3600000UL;
}

/* ---------------- comandos ---------------- */
void infoFS() {
  if (!fs_ok) { Serial.println("# FS: indisponivel"); return; }
  size_t tot = LittleFS.totalBytes(), usa = LittleFS.usedBytes();
  uint32_t tam = tamanhoLog();
  Serial.printf("# FS: total=%u usado=%u livre=%u | log.csv=%lu bytes | boot=%u | local=%s\n",
                (unsigned)tot, (unsigned)usa, (unsigned)(tot - usa),
                (unsigned long)tam, (unsigned)nboot, LOCAL);
  Serial.printf("# envio: ponteiro=%lu de %lu bytes (%.1f %% enviado) | pendente=%lu bytes\n",
                (unsigned long)pos_enviada, (unsigned long)tam,
                tam ? 100.0 * pos_enviada / tam : 0.0,
                (unsigned long)(tam > pos_enviada ? tam - pos_enviada : 0));
  long livre = (long)tot - (long)usa - (long)RESERVA;
  if (livre <= 0 || cheio) {
    Serial.println("# *** FLASH CHEIA - A GRAVACAO ESTA PARADA ***");
    Serial.println("# *** baixe o log com D e apague com Z antes de continuar ***");
  } else {
    Serial.printf("# autonomia restante estimada: %.1f dias a %lu s por linha\n",
                  livre / ((86400.0 / (INTERVALO / 1000.0)) * 85.0),
                  (unsigned long)(INTERVALO / 1000));
  }
}

void despejar() {
  Serial.println("# ---- INICIO DO DUMP ----");
  File f = LittleFS.open(ARQ_LOG, FILE_READ);
  if (!f) { Serial.println("# (log vazio)"); }
  else {
    uint32_t k = 0;
    while (f.available()) {
      Serial.write(f.read());
      if (++k % 4096 == 0) alimentarWatchdog();   // o dump leva ~1 min
    }
    f.close();
  }
  Serial.println();
  Serial.println("# ---- FIM DO DUMP ----");
}

void apagar() {
  LittleFS.remove(ARQ_LOG);
  LittleFS.remove(ARQ_PONT);       // o ponteiro morre junto com o log
  pos_enviada = 0;
  total_enviadas = 0;
  cheio = false;
  hora_ancorada = false;           // a ancora foi apagada com o log
  File f = LittleFS.open(ARQ_LOG, FILE_WRITE);
  if (f) { f.println(CABECALHO); f.close(); }
  Serial.println("# log apagado, recriado, e ponteiro de envio zerado");
  Serial.println("# ATENCAO: a linha de ANCORA foi apagada junto. Rode W ou E");
  Serial.println("# para gravar uma nova antes de comecar a coleta que vale.");
}

// Varre as redes ao alcance. Serve para duas coisas:
//
//  1. Resolver duvida de nome. Acento, espaco e maiuscula no SSID sao
//     causa comum de "wifi FALHOU" com tudo aparentemente certo. Aqui o
//     nome aparece como o radio o enxerga.
//  2. Medir o sinal EM CADA SALA antes de instalar. Sinal fraco significa
//     mais repeticoes de lote e mais tempo de radio ligado - que e
//     justamente o que se quer minimizar (secao 4.25 do diario).
void varrerRedes() {
  Serial.println("# varrendo redes ao alcance...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks();
  if (n <= 0) {
    Serial.println("# nenhuma rede encontrada");
  } else {
    Serial.printf("# %d redes | o ESP32 so enxerga 2,4 GHz\n", n);
    Serial.println("# sinal  seguranca  nome");
    for (int i = 0; i < n; i++) {
      const char *seg;
      switch (WiFi.encryptionType(i)) {
        case WIFI_AUTH_OPEN:            seg = "aberta   "; break;
        case WIFI_AUTH_WEP:             seg = "WEP      "; break;
        case WIFI_AUTH_WPA_PSK:         seg = "WPA-PSK  "; break;
        case WIFI_AUTH_WPA2_PSK:        seg = "WPA2-PSK "; break;
        case WIFI_AUTH_WPA_WPA2_PSK:    seg = "WPA/WPA2 "; break;
        // WPA2-Enterprise cai no default de proposito: o nome do enum
        // mudou entre versoes do core e citar o antigo quebra a
        // compilacao nas novas.
        default:                        seg = "outra    "; break;
      }
      // O nome vai entre colchetes para revelar espaco no inicio ou no fim.
      Serial.printf("# %4d   %s  [%s]%s\n",
                    WiFi.RSSI(i), seg, WiFi.SSID(i).c_str(),
                    WiFi.SSID(i).equals(WIFI_SSID) ? "  <== a configurada" : "");
    }
    Serial.println("# se a sua rede aparece mas NAO tem a marca <==, o nome");
    Serial.println("# configurado esta diferente. Copie o texto entre colchetes.");
  }
  alimentarWatchdog();
  WiFi.scanDelete();
  desligarWiFi();
  Serial.println("# radio desligado");
}

// Zera o ponteiro SEM apagar o log. Ferramenta de TESTE: permite
// reenviar um log inteiro de proposito, para exercitar o envio em
// varios lotes sem ter que esperar horas acumulando linhas.
//
// Tambem serve em campo, se um envio for para a planilha errada ou se
// linhas se perderem do lado do servidor: reenviar duplica, e duplicata
// e removivel pelas colunas boot e min. Perda nao e.
void zerarPonteiro() {
  pos_enviada = 0;
  total_enviadas = 0;
  gravarPonteiro();
  Serial.println("# ponteiro de envio ZERADO - o log NAO foi apagado");
  Serial.println("# a proxima passagem reenvia o arquivo inteiro");
  Serial.printf("# %lu bytes voltaram a ficar pendentes\n",
                (unsigned long)tamanhoLog());
}

void testarRede() {
  Serial.println("# teste de rede - NAO envia dado nenhum");
  if (conectarWiFi()) ancorarHora();
  desligarWiFi();
  Serial.println("# radio desligado");
}

void comandos() {
  while (Serial.available()) {
    char c = Serial.read();
    if      (c == 'I' || c == 'i') infoFS();
    else if (c == 'D' || c == 'd') despejar();
    else if (c == 'Z' || c == 'z') apagar();
    else if (c == 'E' || c == 'e') passagemEnvio(true);
    else if (c == 'W' || c == 'w') testarRede();
    else if (c == 'R' || c == 'r') zerarPonteiro();
    else if (c == 'S' || c == 's') varrerRedes();
  }
}

/* ---------------- setup ---------------- */
void setup() {
  Serial.begin(115200);
  delay(500);

  // Radio desligado por padrao. So liga na passagem de envio.
  WiFi.mode(WIFI_OFF);

  analogReadResolution(12);
  analogSetAttenuation(ADC_6db);
  analogRead(PIN_AO);
  analogSetPinAttenuation(PIN_AO, ADC_6db);

  pinMode(PIN_DHT, INPUT_PULLUP);

  fs_ok = LittleFS.begin(true);
  if (fs_ok) {
    File b = LittleFS.open(ARQ_BOOT, FILE_READ);
    if (b) { nboot = b.parseInt(); b.close(); }
    nboot++;
    b = LittleFS.open(ARQ_BOOT, FILE_WRITE);
    if (b) { b.println(nboot); b.close(); }

    if (!LittleFS.exists(ARQ_LOG)) {
      File f = LittleFS.open(ARQ_LOG, FILE_WRITE);
      if (f) { f.println(CABECALHO); f.close(); }
    }
    lerPonteiro();

    char marca[160];
    snprintf(marca, sizeof(marca),
             "# BOOT %lu local=%s motivo=%s - contador de minutos zerado neste instante",
             (unsigned long)nboot, LOCAL, motivoReset());
    gravar(marca);
  }

  delay(1500);

  Serial.println();
  Serial.printf("# BOOT %lu - firmware v15-CAMPO - local=%s\n",
                (unsigned long)nboot, LOCAL);
  Serial.printf("# motivo do reinicio: %s\n", motivoReset());
  Serial.printf("# DIVISOR=%.3f RL=%.1f | VCC em GPIO%d, K=%.4f, reserva=%.3f\n",
                DIVISOR, RL, PIN_VCC, K_VCC, VCC_RESERVA);
  Serial.printf("# ADC afim: V = %.6e*adc %+.4f\n", ADC_A, ADC_B);
  Serial.println(fs_ok ? "# LittleFS montado - gravando em /log.csv" : "# LittleFS FALHOU");
  Serial.printf("# primeira passagem em %lu min, depois a cada %lu h | lote de %u linhas | radio DESLIGADO entre passagens\n",
                (unsigned long)MIN_PRIMEIRA_PASSAGEM, (unsigned long)HORAS_ENTRE_ENVIOS, LOTE);
  Serial.printf("# watchdog: %lu s\n", (unsigned long)WDT_S);
  Serial.println("# a flash NUNCA e apagada automaticamente - use Z depois de conferir a planilha");
  infoFS();
  if (LOTE < 60) {
    Serial.printf("# >>> LOTE = %u. E VALOR DE TESTE. Volte para 60 antes da coleta. <<<\n",
                  LOTE);
  }
  if (configIncompleta()) {
    Serial.println("# ***************************************************");
    Serial.println("# *** CONFIGURACAO INCOMPLETA - o envio NAO vai    ***");
    Serial.println("# *** funcionar. Preencha WIFI_SENHA e CHAVE no   ***");
    Serial.println("# *** topo do arquivo e suba de novo.             ***");
    Serial.println("# *** A GRAVACAO NA FLASH funciona normalmente.   ***");
    Serial.println("# ***************************************************");
  }
  Serial.println("# comandos: I=info  D=dump  Z=apagar  E=enviar agora");
  Serial.println("#            W=testar rede  R=zerar ponteiro  S=varrer redes");
  Serial.println(CABECALHO);

  prox_envio_ms = millis() + MIN_PRIMEIRA_PASSAGEM * 60000UL;
  iniciarWatchdog();
}

/* ---------------- loop ---------------- */
void loop() {
  alimentarWatchdog();
  comandos();

  // A passagem de envio roda ENTRE janelas, nunca dentro de uma.
  if ((int32_t)(millis() - prox_envio_ms) >= 0) passagemEnvio(false);

  float adc_vcc = 0;
  float vcc = lerVCC(adc_vcc);

  float t = NAN, ur = NAN;
  if (!lerDHT11(PIN_DHT, t, ur)) {
    delay(1500);
    if (!lerDHT11(PIN_DHT, t, ur)) { t = NAN; ur = NAN; }
  }

  uint32_t t0 = millis();
  double soma = 0, soma2 = 0;
  uint32_t n = 0, nout = 0;
  uint16_t vmin = 4095, vmax = 0;
  while (millis() - t0 < INTERVALO) {
    uint16_t x = analogRead(PIN_AO);
    soma += x; soma2 += (double)x * x; n++;
    if (x < vmin) vmin = x;
    if (x > vmax) vmax = x;
    if (x < LIM_OUT) nout++;
    delay(20);
    if ((n & 255) == 0) alimentarWatchdog();
  }

  float  adc = soma / n;
  double var = soma2 / n - (double)adc * adc;
  float  dp  = (var > 0) ? sqrt(var) : 0;
  float  vno = ADC_A * adc + ADC_B;
  float  v   = vno * DIVISOR;
  float  rs  = (v > 0.005) ? RL * (vcc - v) / v : NAN;
  const char *e = adc < 200 ? "baixo" : adc > 3900 ? "ALTO - saturando" : "faixa util";

  char linha[224], tur[24];
  if (isnan(t)) snprintf(tur, sizeof(tur), "NaN,NaN");
  else          snprintf(tur, sizeof(tur), "%.1f,%.1f", t, ur);

  snprintf(linha, sizeof(linha),
           "%lu,%.1f,%.0f,%.1f,%u,%u,%lu,%lu,%.4f,%.4f,%.1f,%s,%.0f,%.3f,%s",
           (unsigned long)nboot, millis() / 60000.0, adc, dp, vmin, vmax,
           (unsigned long)nout, (unsigned long)n, vno, v, rs, tur,
           adc_vcc, vcc, e);

  Serial.println(linha);
  gravar(linha);
}
