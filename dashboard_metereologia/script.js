// conecta via WebSocket seguro (WSS) na porta 8884 do HiveMQ
const client = mqtt.connect(
  "wss://172f401462374e578ec7d0cecad359b9.s1.eu.hivemq.cloud:8884/mqtt",
  {
    username: "Lucas",
    password: "Maxprint123@",
  }
);

const temperaturaEl = document.getElementById("temperatura");
const umidadeEl = document.getElementById("umidade");
const luminosidadeEl = document.getElementById("luminosidade");
const alertaEl = document.getElementById("alerta");
const statusEl = document.getElementById("status");

const btnLedOn = document.getElementById("btnLedOn");
const btnLedOff = document.getElementById("btnLedOff");
const ledStatus = document.getElementById("ledStatus");
const btnEnviarMensagem = document.getElementById("btnEnviarMensagem");
const mensagemTexto = document.getElementById("mensagemTexto");
const ultimaMensagem = document.getElementById("ultimaMensagem");

// mantém só os últimos 15 pontos no gráfico pra não ficar pesado
const labels = [];
const dadosTemp = [];
const ctx = document.getElementById("tempChart");
const chart = new Chart(ctx, {
  type: "line",
  data: {
    labels,
    datasets: [
      {
        label: "Temperatura",
        data: dadosTemp,
        borderColor: "rgb(75, 192, 192)",
        backgroundColor: "rgba(75, 192, 192, 0.2)",
        tension: 0.1,
      },
    ],
  },
  options: { responsive: true },
});

// usados pra atualizar o card de alerta sempre que chegar dado novo
let temperaturaAtual = "0";
let classificacaoAtual = "NORMAL";

client.on("connect", () => {
  console.log("MQTT conectado");
  statusEl.innerText = "ONLINE";
  statusEl.style.color = "green";
  
  // wildcard + e # permitem receber qualquer cidade, bairro e sensor dentro de clima
  // assim o dashboard funciona sem precisar mudar o código se adicionar novos sensores
  client.subscribe("clima/+/+/#"); 
  client.subscribe("alertas/#");
  client.subscribe("status/#");
  client.subscribe("controle/#");
  client.subscribe("dispositivo/led/status");
  client.subscribe("esp32/mensagem/resposta");
});

client.on("error", (err) => {
  console.error(err);
  statusEl.innerText = "ERRO";
  statusEl.style.color = "red";
});

client.on("message", (topic, message) => {
  const valor = message.toString();
  console.log(topic, valor);

  if (topic === "clima/tupaciguara/centro/temperatura") {
    temperaturaAtual = valor;
    temperaturaEl.innerText = `${valor} °C`;
    // adiciona o ponto no gráfico com o horário atual
    labels.push(new Date().toLocaleTimeString());
    dadosTemp.push(parseFloat(valor));
    if (labels.length > 15) {
      labels.shift();
      dadosTemp.shift();
    }
    chart.update();
    atualizarCardAlerta();
  }

  // classificação já vem processada pelo ESP32, só exibe aqui
  if (topic === "clima/tupaciguara/centro/classificacao") {
    classificacaoAtual = valor;
    atualizarCardAlerta();
  }

  if (topic === "clima/tupaciguara/centro/umidade") {
    umidadeEl.innerText = `${valor} %`;
  }
  
  if (topic === "clima/tupaciguara/centro/luminosidade") {
    luminosidadeEl.innerText = `${valor} %`;
  }

  // retained message: esse tópico usa LWT no ESP32, então o broker publica
  // OFFLINE automaticamente se a conexão cair sem aviso
  if (topic === "status/estacao01") {
    statusEl.innerText = valor;
    statusEl.style.color = valor === "ONLINE" ? "green" : "red";
  }

  // confirmação do ESP32 de que o comando foi executado
  if (topic === "dispositivo/led/status") {
    ledStatus.innerHTML = `Status: ${valor === "on" ? "Ligado" : "Desligado"}`;
    ledStatus.style.color = valor === "on" ? "#28a745" : "#dc3545";
  }

  if (topic === "esp32/mensagem/resposta") {
    mostrarMensagem(`Resposta do ESP32: ${valor}`, "success");
  }

  // alerta vem em JSON com campo "alerta" e "valor"
  if (topic === "alertas/temperatura") {
    try {
      const alerta = JSON.parse(valor);
      if (alerta.alerta.includes("Alta")) {
        alertaEl.style.color = "red";
        alertaEl.innerHTML = `<strong>${alerta.alerta}</strong><br>Valor: ${alerta.valor} °C`;
      } else {
        alertaEl.style.color = "green";
        alertaEl.innerHTML = `<strong>${alerta.alerta}</strong><br>Valor: ${alerta.valor} °C`;
      }
    } catch {
      alertaEl.innerText = valor;
    }
  }
});

// atualiza o card de alerta com cor e texto conforme a classificação atual
function atualizarCardAlerta() {
  if (classificacaoAtual === "ALTA") {
    alertaEl.style.color = "red";
    alertaEl.innerHTML = `
      <strong>ALERTA: Temperatura Alta</strong><br>
      Temperatura Atual: ${temperaturaAtual} °C<br>
      Classificação: <span style="font-weight:bold;">ALTA</span>
    `;
  } else if (classificacaoAtual === "FRIA") {
    alertaEl.style.color = "blue";
    alertaEl.innerHTML = `
      <strong>Atenção: Temperatura Baixa</strong><br>
      Temperatura Atual: ${temperaturaAtual} °C<br>
      Classificação: <span style="font-weight:bold;">FRIA</span>
    `;
  } else {
    alertaEl.style.color = "green";
    alertaEl.innerHTML = `
      <strong>Temperatura Normalizada</strong><br>
      Temperatura Atual: ${temperaturaAtual} °C<br>
      Classificação: <span style="font-weight:bold;">NORMAL</span>
    `;
  }
}

function mostrarMensagem(texto, tipo) {
  ultimaMensagem.innerHTML = texto;
  ultimaMensagem.className = `ultima-mensagem show ${tipo}`;
  // some depois de 10 segundos
  setTimeout(() => {
    ultimaMensagem.classList.remove("show");
  }, 10000);
}

// publica no controle/led e também manda mensagem de texto pro ESP32
// assim aparece no serial monitor do Wokwi também
btnLedOn.addEventListener("click", () => {
  client.publish("controle/led", "on");
  client.publish("esp32/mensagem", "Led ligado através do dashboard");
  ledStatus.innerHTML = "Status: Enviando comando...";
  ledStatus.style.color = "#ffc107";
  mostrarMensagem(`Comando enviado: LED LIGADO`, "success");
});

btnLedOff.addEventListener("click", () => {
  client.publish("controle/led", "off");
  client.publish("esp32/mensagem", "Led desligado através do dashboard");
  ledStatus.innerHTML = "Status: Enviando comando...";
  ledStatus.style.color = "#ffc107";
  mostrarMensagem(`Comando enviado: LED DESLIGADO`, "success");
});

btnEnviarMensagem.addEventListener("click", () => {
  let mensagem = mensagemTexto.value.trim();
  if (mensagem === "") {
    mostrarMensagem("Digite uma mensagem antes de enviar!", "error");
    return;
  }
  client.publish("esp32/mensagem", mensagem);
  mostrarMensagem(`Mensagem enviada: "${mensagem}"`, "success");
  mensagemTexto.value = "";
});