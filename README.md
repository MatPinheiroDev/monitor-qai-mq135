# Monitor de qualidade do ar com MQ-135 e ESP32 — dados e código do TCC

Repositório do trabalho de conclusão de curso **"Sistema embarcado de baixo custo para monitoramento da qualidade do ar em ambientes de ensino com registro em nuvem"**, de Mateus Pinheiro Alcântara, Engenharia Eletrônica, Escola Politécnica de Pernambuco (POLI/UPE), 2026.

O repositório traz o firmware do protótipo, os dados brutos das campanhas de bancada e de campo e os scripts que geram as análises e as figuras do artigo.

## Estrutura

```
firmware/
  mq135_v15_campo/   firmware usado na sala I09 (5 a 8/10/2026)
  mq135_v16_campo/   firmware usado na sala K13 (13 a 16/10/2026), com rede reserva
  mq135_v17/         versão posterior, testada só em bancada (escolhe o ponto de acesso mais forte e repete envios que falharem)
dados/
  planilha_completa_1710.csv   todas as linhas recebidas pela planilha em nuvem, de 29/09 a 17/10/2026
analise/
  contar_degraus.py      grade de horários das salas e contagem de degraus de ocupação
  analise_degraus.py     leitura dos dados, relógio pelas âncoras NTP, Δ por degrau e pseudo-degraus
  gerar_figs_artigo.py   Figuras 1, 2 e 3 do artigo
  gerar_fig4_coluna.py   Figura 4 do artigo (índice nas duas salas)
figuras/   figuras do artigo, como geradas pelos scripts
fotos/     fotos usadas nas Figuras 1 e 3
```

## Hardware

ESP32 DevKit V1, módulo MQ-135 e DHT11 de 4 pinos em protoboard, alimentados pela saída USB de uma régua de energia.

| Ligação | Pino do ESP32 |
|---|---|
| Saída A0 do MQ-135, por divisor 213 kΩ / 215 kΩ, com capacitor de 4,7 nF ao terra | GPIO34 (ADC1) |
| Trilho de 5 V, por divisor 440 kΩ (2 × 220 kΩ) / 100 kΩ | GPIO35 (ADC1) |
| DHT11, com pull-up de 10 kΩ | GPIO4 |

Calibração do conversor (atenuação de 6 dB): V = 4,3332 × 10⁻⁴ · N + 0,0199 V no pino; fator do divisor 1,991; carga efetiva R_L = 135,4 kΩ.

## Como usar o firmware

1. Instale a Arduino IDE e o pacote de placas ESP32 (Espressif).
2. Abra o `.ino` desejado.
3. Preencha os campos marcados com `<<<< ... >>>>`: nome e senha da rede, a URL do Web App do Google Apps Script e a chave compartilhada com o script. **As credenciais originais foram removidas deste repositório.**
4. Selecione a placa "ESP32 Dev Module" e grave.

O aparelho grava uma linha a cada ~30 s na memória flash (LittleFS) e envia os dados à planilha a cada ~6 h. Comandos pelo monitor serial (115200 baud): `I` informações da memória, `D` despejo do arquivo, `E` envio imediato, `W` teste de rede e hora, `S` lista as redes visíveis, `R` zera o ponteiro de envio, `Z` apaga a memória.

## Dados

Cada linha de dados corresponde a uma janela de 30 s. Linhas cuja coluna `boot` começa com `#` são marcas do próprio aparelho (`# BOOT`, `# ANCORA`, `# ENVIO INICIO/FIM`).

| Coluna | Significado |
|---|---|
| recebido_em | data de chegada à planilha |
| local | sala gravada no firmware (BANCADA, I09 ou K13) |
| boot | número do religamento do aparelho |
| min | minutos desde o religamento (base do relógio; use esta coluna, não a posição da linha) |
| adc | média das ~1.500 contagens da janela |
| dp | desvio-padrão das contagens dentro da janela |
| adc_min, adc_max, n_out | menor e maior contagem; amostras abaixo de 100 contagens |
| n | número de amostras na janela |
| V_no, V_sensor | tensão no pino e na saída do módulo (V) |
| Rs_kohm | resistência do sensor calculada (kΩ) |
| t_c, ur_pct | temperatura (°C) e umidade relativa (%) do DHT11 |
| adc_vcc, vcc | leitura e tensão de alimentação medida (V) |
| estado | classificação da faixa de leitura |

Observações:
- Há linhas reenviadas: **deduplique por (`boot`, `min`)**.
- O horário absoluto vem das marcas `# ANCORA`, que associam a hora do NTP a um valor de `min`.
- Salas: I09 = boots 50 e 51; K13 = boot 59. Os boots 60 a 63 são ensaios de ruído na K13 após a retirada; o boot 64 é uma noite em residência (gravado com `local=K13`).

## Como reproduzir as análises

Requer Python 3 com `numpy`, `matplotlib` e `Pillow`. Rode a partir da pasta raiz do repositório:

```
pip install -r requirements.txt
python analise/gerar_fig4_coluna.py
python analise/gerar_figs_artigo.py
```

## Licença

Código sob licença MIT (arquivo `LICENSE`). Dados, fotos e figuras sob Creative Commons Atribuição 4.0 (CC BY 4.0).

## Como citar

ALCÂNTARA, M. P. **Sistema embarcado de baixo custo para monitoramento da qualidade do ar em ambientes de ensino com registro em nuvem**. Orientador: Paulo Hugo Espirito Santo Lima. 2026. Trabalho de Conclusão de Curso (Engenharia Eletrônica) – Escola Politécnica de Pernambuco, Universidade de Pernambuco, Recife, 2026.
