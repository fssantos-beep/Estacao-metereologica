#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

const char* ssid = "Wokwi-GUEST";
const char* password = "";

const char* mqtt_server = "172f401462374e578ec7d0cecad359b9.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char* mqtt_user = "Lucas";
const char* mqtt_password = "Maxprint123@";

WiFiClientSecure espClient;
PubSubClient client(espClient);

// pino 2 é o LED interno do ESP32
#define LED_PIN 2
// cada potenciômetro representa um sensor diferente
#define TEMP_PIN 34
#define UMID_PIN 35
#define LUMINO_PIN 32

// controla se o alerta já foi disparado pra não ficar publicando toda hora
bool alertaTemperaturaAtivo = false;
bool ledState = false;
// guarda a classificação anterior pra só publicar quando mudar
String ultimaClassificacao = "";

void reconnect() {
  while (!client.connected()) {
    Serial.print("Conectando MQTT... ");
    // o quinto parâmetro é o LWT: se o ESP32 cair sem avisar, o broker publica "OFFLINE" sozinho
    if (client.connect("ESP32_Clima", mqtt_user, mqtt_password, "status/estacao01", 1, true, "OFFLINE")) {
      Serial.println("Conectado!");
      // retain true: quem conectar depois já recebe o ONLINE sem precisar esperar
      client.publish("status/estacao01", "ONLINE", true);
      
      client.subscribe("controle/led");
      client.subscribe("esp32/mensagem");
    } else {
      Serial.print("Falhou. Codigo: ");
      Serial.println(client.state());
      delay(3000);
    }
  }
}

// chamado automaticamente pelo PubSubClient quando chega mensagem
void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  String topicStr = String(topic);
  Serial.print("Tópico recebido: ");
  Serial.println(topicStr);
  
  if (topicStr == "controle/led") {
    if (message == "on") {
      digitalWrite(LED_PIN, HIGH);
      ledState = true;
      // confirma pro dashboard que o comando foi executado
      client.publish("dispositivo/led/status", "on");
      Serial.println("LED LIGADO pelo dashboard!");
    } else if (message == "off") {
      digitalWrite(LED_PIN, LOW);
      ledState = false;
      client.publish("dispositivo/led/status", "off");
      Serial.println("LED DESLIGADO pelo dashboard!");
    }
  } else if (topicStr == "esp32/mensagem") {
    Serial.print("Mensagem recebida: ");
    Serial.println(message);
    String resposta = "Recebi sua mensagem: \"" + message + "\"";
    client.publish("esp32/mensagem/resposta", resposta.c_str());
    
    Serial.println("Resposta enviada ao dashboard");
    // permite controlar o LED também por texto, ex: "liga" ou "desliga"
    message.toLowerCase();
    
    if (message.indexOf("desliga") >= 0) {
      digitalWrite(LED_PIN, LOW);
      ledState = false;
      client.publish("dispositivo/led/status", "off");
    }
    else if (message.indexOf("liga") >= 0) {
      digitalWrite(LED_PIN, HIGH);
      ledState = true;
      client.publish("dispositivo/led/status", "on");
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  Serial.println("Conectando WiFi...");
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi conectado!");
  
  // setInsecure() ignora verificação de certificado, suficiente pra simulação no Wokwi
  espClient.setInsecure();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  int tempRaw = analogRead(TEMP_PIN);
  int umidRaw = analogRead(UMID_PIN);
  int luminoRaw = analogRead(LUMINO_PIN);

  // potenciômetro vai de 0 a 4095 (12 bits), convertemos pra faixas reais
  float temperatura = (tempRaw / 4095.0) * 50.0;   // 0 a 50°C
  float umidade = (umidRaw / 4095.0) * 100.0;       // 0 a 100%
  float luminosidade = (luminoRaw / 4095.0) * 100.0; // 0 a 100%

  // limites definidos junto com a lógica do dashboard
  String classificacao;
  if (temperatura >= 32) classificacao = "ALTA";
  else if (temperatura < 20) classificacao = "FRIA";
  else classificacao = "NORMAL";

  // só loga no serial quando muda, pra não poluir o monitor
  if (classificacao != ultimaClassificacao) {
    if (classificacao == "ALTA") {
      Serial.println("AVISO: Temperatura ALTA detectada!");
    } else if (classificacao == "FRIA") {
      Serial.println("AVISO: Temperatura FRIA detectada!");
    } else {
      Serial.println("Temperatura voltou ao normal.");
    }
    ultimaClassificacao = classificacao;
  }

  // dtostrf converte float pra string sem usar sprintf (mais seguro no ESP32)
  char tempStr[10], umidStr[10], luminoStr[10];
  dtostrf(temperatura, 4, 0, tempStr);
  dtostrf(umidade, 4, 0, umidStr);
  dtostrf(luminosidade, 4, 0, luminoStr);

  // QoS 0 aqui porque os dados são publicados a cada 5s, perda pontual não é crítica
  client.publish("clima/tupaciguara/centro/temperatura", tempStr);
  client.publish("clima/tupaciguara/centro/umidade", umidStr);
  client.publish("clima/tupaciguara/centro/luminosidade", luminoStr);
  client.publish("clima/tupaciguara/centro/classificacao", classificacao.c_str());

  // alerta com retain=true: novo cliente já recebe o último estado ao conectar
  // flag booleana evita publicar o mesmo alerta repetidamente
  if (temperatura >= 32 && !alertaTemperaturaAtivo) {
    String alerta = "{\"alerta\":\"Temperatura Alta\",\"valor\":" + String(temperatura, 0) + "}";
    client.publish("alertas/temperatura", alerta.c_str(), true);
    alertaTemperaturaAtivo = true;
  } else if (temperatura < 32 && temperatura >= 20 && alertaTemperaturaAtivo) {
    String alerta = "{\"alerta\":\"Temperatura Normalizada\",\"valor\":" + String(temperatura, 0) + "}";
    client.publish("alertas/temperatura", alerta.c_str(), true);
    alertaTemperaturaAtivo = false;
  } else if (temperatura < 20 && !alertaTemperaturaAtivo) {
    String alerta = "{\"alerta\":\"Temperatura Fria\",\"valor\":" + String(temperatura, 0) + "}";
    client.publish("alertas/temperatura", alerta.c_str(), true);
    alertaTemperaturaAtivo = true;
  }

  Serial.printf("Temp: %.0f°C | Umid: %.0f%% | Lum: %.0f%% | LED: %s\n", 
                temperatura, umidade, luminosidade, ledState ? "LIGADO" : "DESLIGADO");
  delay(5000);
}
