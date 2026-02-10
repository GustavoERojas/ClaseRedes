const log = document.getElementById("log");
const btnOn = document.getElementById("btnOn");
const btnOff = document.getElementById("btnOff");

function writeLog(msg) {
  log.textContent = msg + "\n" + log.textContent;
}

async function setLed(state) {
  const r = await fetch("/set_led", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ state })
  });

  const j = await r.json();
  if (j.ok) writeLog(`${j.cmd} -> ${j.resp}`);
  else writeLog(`ERROR -> ${j.error || j.resp}`);
}

btnOn.addEventListener("click", () => setLed(1));
btnOff.addEventListener("click", () => setLed(0));
