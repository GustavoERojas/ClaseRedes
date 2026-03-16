const logEl = document.getElementById('log');
const clearBtn = document.getElementById('clearLog');

// Elementos Humedad
const humedadSlider = document.getElementById('humedadSlider');
const humedadInput = document.getElementById('humedadInput');
const humedadValor = document.getElementById('humedadValor');
const humedadBtn = document.getElementById('humedadBtn');
const ledHumedad = document.getElementById('ledHumedad');

// Elementos Temperatura
const tempSlider = document.getElementById('tempSlider');
const tempInput = document.getElementById('tempInput');
const tempValor = document.getElementById('tempValor');
const tempBtn = document.getElementById('tempBtn');
const ledTemperatura = document.getElementById('ledTemperatura');

function writeLog(msg) {
    const timestamp = new Date().toLocaleTimeString();
    logEl.textContent = `[${timestamp}] ${msg}\n${logEl.textContent}`;
}

// Actualizar LED de humedad (rojo, amarillo, verde)
function actualizarLedHumedad(valor) {
    ledHumedad.classList.remove('led-red-hum', 'led-yellow', 'led-green-hum');
    
    if (valor <= 8) {
        ledHumedad.classList.add('led-red-hum');
        writeLog(`💧 HUMEDAD: ${valor}% → 🔴 ROJO (críticamente seco)`);
    } else if (valor <= 30) {
        ledHumedad.classList.add('led-yellow');
        writeLog(`💧 HUMEDAD: ${valor}% → 🟡 AMARILLO (advertencia)`);
    } else {
        ledHumedad.classList.add('led-green-hum');
        writeLog(`💧 HUMEDAD: ${valor}% → 🟢 VERDE (óptimo)`);
    }
}

// Actualizar LED de temperatura (azul, verde, rojo)
function actualizarLedTemperatura(valor) {
    ledTemperatura.classList.remove('led-blue', 'led-green', 'led-red');
    
    if (valor < 10) {
        ledTemperatura.classList.add('led-blue');
        writeLog(`🌡️ TEMPERATURA: ${valor}°C → 🔵 AZUL (muy frío)`);
    } else if (valor <= 25) {
        ledTemperatura.classList.add('led-green');
        writeLog(`🌡️ TEMPERATURA: ${valor}°C → 🟢 VERDE (temperatura ideal)`);
    } else {
        ledTemperatura.classList.add('led-red');
        writeLog(`🌡️ TEMPERATURA: ${valor}°C → 🔴 ROJO (muy caliente)`);
    }
}

// Actualizar barra de progreso del slider
function updateSliderProgress(slider) {
    const min = slider.min || 0;
    const max = slider.max || 100;
    const val = slider.value;
    const percentage = ((val - min) / (max - min)) * 100;
    slider.style.setProperty('--val', percentage + '%');
}

// Actualizar valor de humedad
function setHumedad(valor) {
    valor = Math.min(100, Math.max(0, Number(valor)));
    
    humedadValor.textContent = valor + '%';
    humedadSlider.value = valor;
    humedadInput.value = valor;
    
    updateSliderProgress(humedadSlider);
    actualizarLedHumedad(valor);
    
    fetch('/api/sensores', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ sensor: 'humedad', valor: valor })
    });
}

// Actualizar valor de temperatura
function setTemperatura(valor) {
    valor = Math.min(50, Math.max(-10, Number(valor)));
    
    tempValor.textContent = valor + '°C';
    tempSlider.value = valor;
    tempInput.value = valor;
    
    updateSliderProgress(tempSlider);
    actualizarLedTemperatura(valor);
    
    fetch('/api/sensores', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ sensor: 'temperatura', valor: valor })
    });
}

// Cargar valores iniciales
async function cargarValoresIniciales() {
    try {
        const response = await fetch('/api/sensores');
        const data = await response.json();
        
        setHumedad(data.humedad);
        setTemperatura(data.temperatura);
        
        writeLog('✅ Sistema inicializado correctamente');
    } catch (error) {
        writeLog('❌ Error cargando valores iniciales');
    }
}

// Event listeners para efecto hover en tarjetas
document.querySelectorAll('.sensor-card').forEach(card => {
    card.addEventListener('mousemove', (e) => {
        const rect = card.getBoundingClientRect();
        const x = ((e.clientX - rect.left) / rect.width) * 100;
        const y = ((e.clientY - rect.top) / rect.height) * 100;
        card.style.setProperty('--x', x + '%');
        card.style.setProperty('--y', y + '%');
    });
});

// Event listeners Humedad
humedadSlider.addEventListener('input', (e) => {
    setHumedad(e.target.value);
});

humedadInput.addEventListener('change', (e) => {
    setHumedad(e.target.value);
});

humedadBtn.addEventListener('click', () => {
    setHumedad(humedadInput.value);
});

// Event listeners Temperatura
tempSlider.addEventListener('input', (e) => {
    setTemperatura(e.target.value);
});

tempInput.addEventListener('change', (e) => {
    setTemperatura(e.target.value);
});

tempBtn.addEventListener('click', () => {
    setTemperatura(tempInput.value);
});

// Botón limpiar log
if (clearBtn) {
    clearBtn.addEventListener('click', () => {
        logEl.textContent = '📋 Log limpiado.\n';
    });
}

// Inicializar sliders
updateSliderProgress(humedadSlider);
updateSliderProgress(tempSlider);

// Cargar datos
cargarValoresIniciales();
