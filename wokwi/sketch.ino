// Cofre Digital - Projeto Integrador (A1/A3) - Sistemas Digitais - UniRitter
// Arthur Schwambach e Henrique Braga Stortti de Souza
//
// Maquina de estados de Moore com 8 estados (Q2 Q1 Q0), logica de decisao e
// saidas implementadas com as expressoes obtidas por Karnaugh na A1.
// Senha de teste: 2580

#include <Keypad.h>
#include <ESP32Servo.h>

// ---------- pinos (Figura 6 da A1) ----------
byte pinosLinhas[4]  = {13, 14, 27, 26};   // R1..R4 (lidas)
byte pinosColunas[4] = {25, 33, 32, 23};   // C1..C4 (ativadas uma por vez)
const int PINO_DS   = 16;                  // 74HC595 - dado
const int PINO_SHCP = 4;                   // 74HC595 - clock de deslocamento
const int PINO_STCP = 17;                  // 74HC595 - latch
const int PINO_SERVO    = 18;
const int PINO_VERDE    = 19;
const int PINO_VERMELHO = 21;
const int PINO_AMARELO  = 22;
const int PINO_BUZZER   = 5;

char teclas[4][4] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};
Keypad teclado = Keypad(makeKeymap(teclas), pinosLinhas, pinosColunas, 4, 4);
Servo trava;

// ---------- estados (codigo Q2 Q1 Q0 da Tabela 6) ----------
enum Estado : uint8_t {
  E_OCIOSO    = 0b000,
  E_D1        = 0b001,
  E_D2        = 0b010,
  E_D3        = 0b011,
  E_D4        = 0b100,
  E_ABERTO    = 0b101,
  E_ERRO      = 0b110,
  E_BLOQUEADO = 0b111
};

const uint16_t SENHA_BCD = 0x2580;          // senha 2580 guardada em BCD (16 bits)
const unsigned long TEMPO_ERRO     = 1500;  // ms
const unsigned long TEMPO_BLOQUEIO = 30000; // ms

Estado estado = E_OCIOSO;
uint8_t tentativas = 0;          // contador de erros T1 T0
uint16_t senhaDigitada = 0;      // digitos digitados, em BCD
unsigned long inicioEstado = 0;  // momento em que entrou no estado atual
unsigned long fimBip = 0;        // fim do bip curto de tecla

// ---------------------------------------------------------------

const char *nomeEstado(Estado e) {
  switch (e) {
    case E_OCIOSO:    return "OCIOSO";
    case E_D1:        return "D1";
    case E_D2:        return "D2";
    case E_D3:        return "D3";
    case E_D4:        return "D4";
    case E_ABERTO:    return "ABERTO";
    case E_ERRO:      return "ERRO";
    case E_BLOQUEADO: return "BLOQUEADO";
  }
  return "?";
}

void mudarEstado(Estado novo) {
  estado = novo;
  inicioEstado = millis();
  Serial.printf("-> %s (%d%d%d)  T = %d\n", nomeEstado(novo),
                (novo >> 2) & 1, (novo >> 1) & 1, novo & 1, tentativas);
}

// Decodificador do display: mostra 3 - T (Secao 7.3 da A1)
void mostrarDisplay(uint8_t t) {
  bool T1 = t & 0b10;
  bool T0 = t & 0b01;
  bool a = !T1 || T0;
  bool b = true;
  bool c = T1 || !T0;
  bool d = !T1 || T0;
  bool e = T0;
  bool f = T1 && T0;
  bool g = !T1;
  byte segmentos = a | (b << 1) | (c << 2) | (d << 3) | (e << 4) | (f << 5) | (g << 6);  // Q0..Q6 = a..g

  digitalWrite(PINO_STCP, LOW);
  shiftOut(PINO_DS, PINO_SHCP, MSBFIRST, segmentos);
  digitalWrite(PINO_STCP, HIGH);
}

// Logica de decisao em D4 quando # e pressionado (Secao 7.1 da A1)
void decidir() {
  bool C = true;                              // chegou aqui porque # foi pressionado
  bool S = (senhaDigitada == SENHA_BCD);
  bool T1 = tentativas & 0b10;

  bool abrir    = C && S;
  bool errar    = C && !S && !T1;
  bool bloquear = C && !S && T1;
  bool I        = C && !S;                    // incrementa o contador (Secao 7.2)

  senhaDigitada = 0;
  if (I) tentativas = (tentativas + 1) & 0b11;

  if (abrir) {
    tentativas = 0;                           // Z = 1: zera o contador
    mudarEstado(E_ABERTO);
  } else if (errar) {
    mudarEstado(E_ERRO);
  } else if (bloquear) {
    mudarEstado(E_BLOQUEADO);
  }
}

void cancelar() {
  senhaDigitada = 0;
  mudarEstado(E_OCIOSO);
}

// Saidas de Moore: dependem so do estado (Secao 7.4 da A1)
void atualizarSaidas(unsigned long agora) {
  bool q2 = estado & 0b100;
  bool q1 = estado & 0b010;
  bool q0 = estado & 0b001;

  bool amarelo  = (!q2 && q0) || (!q2 && q1) || (q2 && !q1 && !q0);
  bool vermelho = q2 && q1;
  bool verde    = q2 && !q1 && q0;           // tambem comanda o servo
  bool buzzer   = q2 && q1;

  // no bloqueio o LED vermelho e o alarme piscam (250 ms ligado / 250 ms desligado)
  if (estado == E_BLOQUEADO) {
    bool pisca = ((agora - inicioEstado) / 250) % 2 == 0;
    vermelho = pisca;
    buzzer = pisca;
  }

  digitalWrite(PINO_AMARELO, amarelo);
  digitalWrite(PINO_VERMELHO, vermelho);
  digitalWrite(PINO_VERDE, verde);

  static bool servoAberto = false;
  if (verde != servoAberto) {
    trava.write(verde ? 90 : 0);
    servoAberto = verde;
  }

  // buzzer: alarme tem prioridade sobre o bip das teclas
  int freq = 0;
  if (buzzer) freq = (estado == E_ERRO) ? 400 : 1000;
  else if (agora < fimBip) freq = 2000;

  static int freqAtual = 0;
  if (freq != freqAtual) {
    if (freq) tone(PINO_BUZZER, freq);
    else noTone(PINO_BUZZER);
    freqAtual = freq;
  }

  static int ultimoValor = -1;
  if (tentativas != ultimoValor) {
    mostrarDisplay(tentativas);
    ultimoValor = tentativas;
  }
}

// ---------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  pinMode(PINO_DS, OUTPUT);
  pinMode(PINO_SHCP, OUTPUT);
  pinMode(PINO_STCP, OUTPUT);
  pinMode(PINO_VERDE, OUTPUT);
  pinMode(PINO_VERMELHO, OUTPUT);
  pinMode(PINO_AMARELO, OUTPUT);

  trava.attach(PINO_SERVO, 500, 2400);
  trava.write(0);

  Serial.println("Cofre digital pronto. Senha de teste: 2580");
  mudarEstado(E_OCIOSO);
}

void loop() {
  char tecla = teclado.getKey();
  unsigned long agora = millis();
  if (tecla) Serial.printf("Tecla: %c\n", tecla);

  switch (estado) {
    case E_OCIOSO:
    case E_D1:
    case E_D2:
    case E_D3:
      if (tecla >= '0' && tecla <= '9') {
        senhaDigitada = (senhaDigitada << 4) | (tecla - '0');   // guarda o digito em BCD
        fimBip = agora + 60;
        mudarEstado((Estado)(estado + 1));                     // 000 -> 001 -> 010 -> 011 -> 100
      } else if (tecla == '*' && estado != E_OCIOSO) {
        cancelar();
      }
      break;

    case E_D4:                                // digitos a mais sao ignorados
      if (tecla == '*') cancelar();
      else if (tecla == '#') decidir();
      break;

    case E_ABERTO:
      if (tecla == '*') mudarEstado(E_OCIOSO);
      break;

    case E_ERRO:
      if (agora - inicioEstado >= TEMPO_ERRO) mudarEstado(E_OCIOSO);
      break;

    case E_BLOQUEADO:                         // teclado ignorado
      if (agora - inicioEstado >= TEMPO_BLOQUEIO) {
        tentativas = 0;                       // Z = 1: fim do bloqueio
        mudarEstado(E_OCIOSO);
      }
      break;
  }

  atualizarSaidas(agora);
}
