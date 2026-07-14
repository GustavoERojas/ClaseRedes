let rpmChart = null;
let rpmHistory = [];
let referenciaHistory = [];
let isUpdating = false;

function getEstadoDisplay(estado) {
    const nombres = {
        'DETENIDO': 'Detenido',
        'PRODUCIENDO': 'Produciendo',
        'PAUSADO': 'Pausado'
    };
    return nombres[estado] || estado;
}

function getColorName(color) {
    const colores = {
        'ROJO': 'Rojo',
        'VERDE': 'Verde',
        'AZUL': 'Azul',
        'NINGUNO': 'Ninguno'
    };
    return colores[color] || color;
}

function getColorHex(color) {
    const colores = {
        'ROJO': '#ef4444',
        'VERDE': '#10b981',
        'AZUL': '#00b4d8',
        'NINGUNO': '#4a4a5a'
    };
    return colores[color] || '#4a4a5a';
}

async function enviarComando(cmd) {
    const btn = event?.target?.closest('.btn');
    if (btn) {
        btn.style.transform = 'scale(0.95)';
        setTimeout(() => { if (btn) btn.style.transform = ''; }, 150);
    }
    
    try {
        const response = await fetch("/control", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ comando: cmd })
        });
        const result = await response.json();
        if (result.ok) {
            addMonitorMessage(result.mensaje, 'success');
        } else {
            addMonitorMessage(result.error || 'Error en el comando', 'error');
        }
    } catch (error) {
        addMonitorMessage('Error de conexión con el sistema', 'error');
    }
}

async function resetSistema() {
    try {
        const response = await fetch("/api/reset", {
            method: "POST",
            headers: { "Content-Type": "application/json" }
        });
        const result = await response.json();
        addMonitorMessage(result.mensaje, result.ok ? 'success' : 'error');
    } catch (error) {
        addMonitorMessage('Error al resetear el sistema', 'error');
    }
}

async function setReferencia() {
    const input = document.getElementById("referencia-input");
    const valor = parseFloat(input.value);
    
    if (isNaN(valor) || valor < 0) {
        addMonitorMessage('Ingrese un valor de RPM válido (mayor a 0)', 'error');
        return;
    }
    
    try {
        const response = await fetch("/api/set_referencia", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ rpm_referencia: valor })
        });
        const data = await response.json();
        if (data.ok) {
            document.getElementById("referencia-value").innerText = data.rpm_referencia.toFixed(0);
            addMonitorMessage(data.mensaje, 'success');
        }
    } catch (error) {
        addMonitorMessage('Error al establecer referencia', 'error');
    }
}

function addMonitorMessage(message, type = 'info') {
    const monitor = document.getElementById("monitor");
    if (!monitor) return;
    
    const timestamp = new Date().toLocaleTimeString();
    
    const line = document.createElement('div');
    line.className = `monitor-line ${type}`;
    line.innerHTML = `<span class="monitor-time">[${timestamp}]</span> ${message}`;
    
    monitor.appendChild(line);
    line.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
    
    while (monitor.children.length > 100) {
        monitor.removeChild(monitor.firstChild);
    }
}

function clearMonitor() {
    const monitor = document.getElementById("monitor");
    if (monitor) {
        monitor.innerHTML = '<div class="monitor-line system">[Sistema] Monitor limpiado</div>';
        addMonitorMessage('Monitor de comunicación limpiado', 'system');
    }
}

function initChart() {
    const canvas = document.getElementById('rpmChart');
    if (!canvas) return;
    if (rpmChart) rpmChart.destroy();
    
    rpmChart = new Chart(canvas.getContext('2d'), {
        type: 'line',
        data: {
            labels: [],
            datasets: [
                {
                    label: 'RPM Real',
                    data: [],
                    borderColor: '#00b4d8',
                    backgroundColor: 'rgba(0, 180, 216, 0.05)',
                    borderWidth: 2.5,
                    pointRadius: 0,
                    pointHoverRadius: 6,
                    tension: 0.3,
                    fill: true
                },
                {
                    label: 'RPM Referencia',
                    data: [],
                    borderColor: '#f59e0b',
                    backgroundColor: 'transparent',
                    borderWidth: 2,
                    borderDash: [8, 4],
                    pointRadius: 0,
                    tension: 0.3,
                    fill: false
                }
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            interaction: { mode: 'index', intersect: false },
            plugins: {
                legend: { 
                    position: 'top', 
                    labels: { 
                        color: '#f3f4f6', 
                        font: { size: 11, weight: '600' }, 
                        usePointStyle: true,
                        boxWidth: 8,
                        padding: 16
                    } 
                },
                tooltip: { 
                    mode: 'index', 
                    intersect: false, 
                    backgroundColor: '#1f2937', 
                    titleColor: '#f3f4f6', 
                    bodyColor: '#9ca3af', 
                    borderColor: '#00b4d8', 
                    borderWidth: 1,
                    padding: 12,
                    callbacks: {
                        label: (ctx) => `${ctx.dataset.label}: ${ctx.parsed.y.toFixed(1)} RPM`
                    }
                }
            },
            scales: {
                x: { 
                    grid: { color: '#1f2937', display: false }, 
                    ticks: { color: '#6b7280', maxTicksLimit: 8 },
                    title: { display: true, text: 'Muestras', color: '#6b7280', font: { size: 10 } }
                },
                y: { 
                    grid: { color: '#1f2937' }, 
                    ticks: { color: '#6b7280', callback: (v) => `${v} RPM` }, 
                    beginAtZero: true,
                    title: { display: true, text: 'Revoluciones por Minuto', color: '#6b7280', font: { size: 10 } }
                }
            }
        }
    });
}

function updateCharts(rpm, referencia) {
    if (!rpmChart) return;
    
    rpmHistory.push(rpm);
    referenciaHistory.push(referencia);
    
    if (rpmHistory.length > 60) {
        rpmHistory.shift();
        referenciaHistory.shift();
    }
    
    rpmChart.data.labels = rpmHistory.map((_, i) => i);
    rpmChart.data.datasets[0].data = [...rpmHistory];
    rpmChart.data.datasets[1].data = [...referenciaHistory];
    rpmChart.update('none');
}

function updateSemaforo(semaforo) {
    const semaforoLeds = document.getElementById('semaforo-leds');
    const semaforoMessage = document.getElementById('semaforo-message');
    
    if (!semaforoLeds) return;
    
    semaforoLeds.classList.remove('verde', 'amarillo', 'rojo');
    
    const mensajes = {
        'VERDE': { msg: 'Sistema optimizado - Error < 5%', color: 'verde' },
        'AMARILLO': { msg: 'Margen de ajuste - Error 5-15%', color: 'amarillo' },
        'ROJO': { msg: 'Fuera de especificación - Error > 15%', color: 'rojo' }
    };
    
    const estado = mensajes[semaforo] || mensajes['ROJO'];
    semaforoLeds.classList.add(estado.color);
    if (semaforoMessage) semaforoMessage.innerText = estado.msg;
}

function updateSensorColor(color, rojo, verde, azul) {
    const colorPreview = document.getElementById('color-preview');
    const colorDetectado = document.getElementById('color-detectado');
    const rojoBar = document.getElementById('rojo-bar');
    const verdeBar = document.getElementById('verde-bar');
    const azulBar = document.getElementById('azul-bar');
    const rojoValor = document.getElementById('rojo-valor');
    const verdeValor = document.getElementById('verde-valor');
    const azulValor = document.getElementById('azul-valor');
    
    // Normalizar valores (0-100)
    const maxValor = Math.max(rojo, verde, azul, 1);
    const rNorm = (rojo / maxValor) * 100;
    const gNorm = (verde / maxValor) * 100;
    const bNorm = (azul / maxValor) * 100;
    
    if (rojoBar) rojoBar.style.width = Math.min(100, rNorm) + '%';
    if (verdeBar) verdeBar.style.width = Math.min(100, gNorm) + '%';
    if (azulBar) azulBar.style.width = Math.min(100, bNorm) + '%';
    
    if (rojoValor) rojoValor.innerText = rojo;
    if (verdeValor) verdeValor.innerText = verde;
    if (azulValor) azulValor.innerText = azul;
    
    const colorName = getColorName(color);
    const colorHex = getColorHex(color);
    
    if (colorDetectado) {
        colorDetectado.innerText = colorName;
        colorDetectado.style.color = colorHex;
        colorPreview.style.background = colorHex;
        colorPreview.style.boxShadow = color !== 'NINGUNO' ? `0 0 20px ${colorHex}40` : 'none';
    }
}

function updateHistorial(historial) {
    const container = document.getElementById('historial-grid');
    if (!container) return;
    
    container.innerHTML = '';
    
    for (let i = 0; i < 10; i++) {
        const item = document.createElement('div');
        if (i < historial.length) {
            const color = historial[i].color;
            const colorName = getColorName(color);
            const colorHex = getColorHex(color);
            const tiempo = historial[i].tiempo || '';
            
            item.className = `historial-item ${color.toLowerCase()}`;
            item.innerHTML = `${colorName}<span class="tiempo">${tiempo}</span>`;
            item.style.borderColor = colorHex;
        } else {
            item.className = 'historial-item vacio';
            item.innerText = '·';
        }
        container.appendChild(item);
    }
}

function updateCounters(rojas, verdes, azules, desconocidas) {
    document.getElementById("piezas-rojas").innerText = rojas;
    document.getElementById("piezas-verdes").innerText = verdes;
    document.getElementById("piezas-azules").innerText = azules;
    document.getElementById("piezas-desconocidas").innerText = desconocidas;
    
    const total = rojas + verdes + azules + desconocidas;
    document.getElementById("piezas-total").innerText = total;
}

function updatePIDInfo(pwm, error) {
    const pwmBar = document.getElementById('pwm-bar');
    const pwmValue = document.getElementById('pwm-value');
    const errorValue = document.getElementById('error-value');
    const estadoValue = document.getElementById('estado-value');
    const rpmValue = document.getElementById('rpm-value');
    const referenciaValue = document.getElementById('referencia-value');
    
    if (pwmBar) pwmBar.style.width = (pwm / 255) * 100 + '%';
    if (pwmValue) pwmValue.innerText = pwm;
    if (errorValue) {
        errorValue.innerText = error.toFixed(1);
        if (Math.abs(error) <= 5) errorValue.style.color = '#10b981';
        else if (Math.abs(error) <= 15) errorValue.style.color = '#f59e0b';
        else errorValue.style.color = '#ef4444';
    }
}

function updateKPI(id, value, suffix = '') {
    const element = document.getElementById(`${id}-value`);
    if (!element) return;
    const newValue = typeof value === 'number' ? value.toFixed ? value.toFixed(1) : value : value;
    if (element.innerText !== `${newValue}${suffix}`) {
        element.style.transform = 'scale(1.05)';
        element.innerText = `${newValue}${suffix}`;
        setTimeout(() => { if (element) element.style.transform = ''; }, 200);
    }
}

function updateEstado(estado) {
    const statusText = document.getElementById('status-text');
    const statusDot = document.getElementById('status-dot');
    const estadoDisplay = document.getElementById('estado-display');
    
    if (statusText) statusText.innerText = getEstadoDisplay(estado);
    if (statusDot) {
        statusDot.className = 'status-dot';
        const classes = {
            'PRODUCIENDO': 'produciendo',
            'DETENIDO': 'detenido',
            'PAUSADO': 'pausado'
        };
        statusDot.classList.add(classes[estado] || 'detenido');
    }
    if (estadoDisplay) estadoDisplay.innerText = getEstadoDisplay(estado);
}

async function actualizarDatos() {
    if (isUpdating) return;
    isUpdating = true;
    
    try {
        const response = await fetch("/api/data");
        const data = await response.json();
        
        if (data.ok) {
            updateKPI('rpm', data.rpm);
            updateKPI('referencia', data.referencia);
            updateKPI('error', data.error);
            updateKPI('pwm', data.pwm);
            
            updateEstado(data.estado);
            updateSemaforo(data.semaforo);
            updatePIDInfo(data.pwm, data.error);
            updateSensorColor(data.color_detectado, data.rojo, data.verde, data.azul);
            updateCounters(data.piezas_rojas, data.piezas_verdes, data.piezas_azules, data.piezas_desconocidas);
            updateHistorial(data.historial_colores || []);
            updateCharts(data.rpm, data.referencia);
            
            // Mostrar el color detectado en el monitor
            if (data.color_detectado && data.color_detectado !== 'NINGUNO') {
                const colorName = getColorName(data.color_detectado);
                const colorHex = getColorHex(data.color_detectado);
                // El monitor ya se actualiza desde el Arduino, no es necesario agregar más mensajes
            }
        }
    } catch (error) {
        console.error('Error:', error);
    } finally {
        isUpdating = false;
    }
}

document.addEventListener('DOMContentLoaded', () => {
    initChart();
    actualizarDatos();
    setInterval(actualizarDatos, 300);
    
    const refInput = document.getElementById("referencia-input");
    if (refInput) {
        refInput.addEventListener("keypress", (e) => {
            if (e.key === "Enter") setReferencia();
        });
    }
    
    addMonitorMessage('Sistema de Clasificación Industrial iniciado', 'system');
    addMonitorMessage('Control PID activo - Esperando detección de piezas', 'system');
});

let resizeTimeout;
window.addEventListener('resize', () => {
    clearTimeout(resizeTimeout);
    resizeTimeout = setTimeout(() => rpmChart?.resize(), 250);
});