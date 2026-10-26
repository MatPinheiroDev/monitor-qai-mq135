/*
  Monitor de qualidade do ar com MQ-135 e ESP32 - firmware de campo v16 (sala K13)
  TCC de Engenharia Eletronica - Escola Politecnica de Pernambuco (POLI/UPE), 2026
  Autor: Mateus Pinheiro Alcantara

  Le o MQ-135 (GPIO34) e a tensao de alimentacao (GPIO35) pelo ADC1, e o DHT11 (GPIO4).
  A cada ~30 s grava na flash (LittleFS) uma linha com a media de ~1.500 amostras.
  A cada ~6 h liga o Wi-Fi, ancora o relogio por NTP e envia as linhas pendentes,
  em lotes, a um Web App do Google Apps Script.
  Tenta a rede principal e, se falhar, a rede reserva.

  Antes de gravar, preencha os campos marcados com <<<< ... >>>>.
  Licenca MIT.
*/

#include <math.h>
#include <string.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

const char *LOCAL = "K13";

#define ENTERPRISE 0

const char *WIFI_SSID  = "<<<< NOME DA REDE >>>>";

const char *WIFI_SENHA = "<<<< SENHA DA REDE >>>>";

const char *WIFI_SSID2  = "<<<< NOME DA REDE RESERVA >>>>";
const char *WIFI_SENHA2 = "<<<< SENHA DA REDE RESERVA >>>>";
const uint32_t ESPERA_WIFI2_MS = 12000;

#if ENTERPRISE

  const char *EAP_IDENTIDADE = "identidade";
  const char *EAP_USUARIO    = "usuario";
  const char *EAP_SENHA      = "<<<< SENHA EAP >>>>";
#endif

const char *URL_NUVEM = "COLOQUE AQUI A URL DO WEB APP (termina em /exec)";

const char *CHAVE = "<<<< CHAVE COMPARTILHADA >>>>";

const uint32_t HORAS_ENTRE_ENVIOS = 6;

const uint32_t MIN_PRIMEIRA_PASSAGEM = 3;

const uint32_t WDT_S = 120;

const uint16_t LOTE               = 60;
const uint8_t  TENTATIVAS         = 3;
const uint32_t TIMEOUT_MS         = 15000;
const uint32_t ESPERA_WIFI_MS     = 20000;

#if ENTERPRISE
  #if ESP_ARDUINO_VERSION_MAJOR >= 3
    #include <esp_eap_client.h>
  #else
    #include <esp_wpa2.h>
  #endif
#endif

bool configIncompleta() {
  return strstr(WIFI_SSID, "<<<<") || strstr(WIFI_SENHA, "<<<<") ||
         strstr(WIFI_SSID2, "<<<<") || strstr(WIFI_SENHA2, "<<<<") || strstr(CHAVE, "<<<<") ||
         strstr(URL_NUVEM, "COLOQUE");
}

#define PIN_AO   34
#define PIN_DHT   4
#define PIN_VCC  35

const float    VCC_RESERVA = 4.98;
const float    K_VCC     = 5.713;
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
const char *ARQ_PONT  = "/envio.txt";
const size_t RESERVA  = 24000;
const char *CABECALHO =
  "boot,min,adc,dp,adc_min,adc_max,n_out,n,V_no,V_sensor,Rs_kohm,t_c,ur_pct,adc_vcc,vcc,estado";

uint32_t nboot = 0;
bool     fs_ok = false;
bool     cheio = false;

uint32_t pos_enviada = 0;
uint32_t prox_envio_ms = 0;
uint32_t total_enviadas = 0;
bool     hora_ancorada = false;

static char lote_buf[10240];

/* ---------------- watchdog e motivo do reinicio ---------------- */

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
  esp_task_wdt_add(NULL);
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

void lerPonteiro() {
  pos_enviada = 0;
  File f = LittleFS.open(ARQ_PONT, FILE_READ);
  if (f) { pos_enviada = (uint32_t)f.parseInt(); f.close(); }
  uint32_t tam = tamanhoLog();
  if (pos_enviada > tam) {

    Serial.println("# ponteiro maior que o log (apagamento?) - zerado");
    pos_enviada = 0;
  }
}

void gravarPonteiro() {
  File f = LittleFS.open(ARQ_PONT, FILE_WRITE);
  if (f) { f.println(pos_enviada); f.close(); }
}

/* ---------------- rede ---------------- */

bool tentarRede(const char *ssid, const char *senha, uint32_t espera) {
  Serial.printf("# tentando rede [%s]\n", ssid);
  WiFi.begin(ssid, senha);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < espera) {
    delay(250);
    alimentarWatchdog();
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("# rede [%s] FALHOU\n", ssid);
    return false;
  }
  char marca[96];
  snprintf(marca, sizeof(marca), "# REDE %s rssi=%d dBm", ssid, WiFi.RSSI());
  gravar(marca);
  Serial.printf("# wifi ok: %s  rssi=%d dBm  rede=[%s]\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI(), ssid);
  return true;
}

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
  if (tentarRede(WIFI_SSID, WIFI_SENHA, ESPERA_WIFI_MS)) return true;
  WiFi.disconnect(true, true);
  delay(200);
  if (tentarRede(WIFI_SSID2, WIFI_SENHA2, ESPERA_WIFI2_MS)) return true;
  Serial.println("# wifi FALHOU nas duas redes");
  return false;
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

void ancorarHora() {
  configTime(-3 * 3600, 0, "pool.ntp.org", "a.st1.ntp.br");
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
  hora_ancorada = true;
}

String resumo(const String &r) {
  String t = r;
  t.replace('\n', ' ');
  t.replace('\r', ' ');
  if (t.length() > 110) t = t.substring(0, 110) + "...";
  return t;
}

/* ---------------- envio de um lote ---------------- */
int enviarLote() {
  File f = LittleFS.open(ARQ_LOG, FILE_READ);
  if (!f) return -1;
  if (!f.seek(pos_enviada)) { f.close(); return -1; }

  size_t usado = 0;
  uint16_t n_linhas = 0;
  uint32_t pos_fim = pos_enviada;

  while (n_linhas < LOTE && f.available()) {
    String linha = f.readStringUntil('\n');

    uint32_t pos_depois = f.position();
    linha.trim();

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

  cli.setInsecure();

  char meta[160];
  snprintf(meta, sizeof(meta), "%s|%s|%lu|%lu|%u\n",
           CHAVE, LOCAL, (unsigned long)nboot,
           (unsigned long)pos_enviada, n_linhas);

  String corpo;
  corpo.reserve(usado + strlen(meta) + 8);
  corpo = meta;
  corpo += lote_buf;

  /* -------- redirecionamento tratado A MAO ---------------- */
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
        delay(2000UL * tent);
      }
      r = enviarLote();
    }
    if (r < 0) { abortou = true; break; }
    if (r == 0) break;
    lotes_ok++;
    if (lotes_ok % 10 == 0)
      Serial.printf("# %u lotes, %lu linhas confirmadas\n",
                    lotes_ok, (unsigned long)total_enviadas);
    delay(150);
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
      if (++k % 4096 == 0) alimentarWatchdog();
    }
    f.close();
  }
  Serial.println();
  Serial.println("# ---- FIM DO DUMP ----");
}

void apagar() {
  LittleFS.remove(ARQ_LOG);
  LittleFS.remove(ARQ_PONT);
  pos_enviada = 0;
  total_enviadas = 0;
  cheio = false;
  hora_ancorada = false;
  File f = LittleFS.open(ARQ_LOG, FILE_WRITE);
  if (f) { f.println(CABECALHO); f.close(); }
  Serial.println("# log apagado, recriado, e ponteiro de envio zerado");
  Serial.println("# ATENCAO: a linha de ANCORA foi apagada junto. Rode W ou E");
  Serial.println("# para gravar uma nova antes de comecar a coleta que vale.");
}

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

        default:                        seg = "outra    "; break;
      }

      Serial.printf("# %4d   %s  [%s]%s\n",
                    WiFi.RSSI(i), seg, WiFi.SSID(i).c_str(),
                    WiFi.SSID(i).equals(WIFI_SSID) ? "  <== principal" : (WiFi.SSID(i).equals(WIFI_SSID2) ? "  <== reserva" : ""));
    }
    Serial.println("# se a sua rede aparece mas NAO tem a marca <==, o nome");
    Serial.println("# configurado esta diferente. Copie o texto entre colchetes.");
  }
  alimentarWatchdog();
  WiFi.scanDelete();
  desligarWiFi();
  Serial.println("# radio desligado");
}

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
  Serial.printf("# BOOT %lu - firmware v16-CAMPO - local=%s\n",
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
