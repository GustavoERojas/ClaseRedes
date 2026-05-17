let rpmChart = null;
let rpmHistory = [];
let referenciaHistory = [];
let isUpdating = false;

function getEstadoDisplay(estado) {
    const nombres = {
        'DERECHA': 'AVANCE',
        'IZQUIERDA': 'REVERSA',
        'PARO': 'DETENIDO'
    };
    return nombres[estado] || estado;
}

function getColorDisplay(color) {
    const colores = {
        'VERDE': '✅ VERDE - PIEZA APTA',
        'LADRILLO': '❌ LADRILLO - PIEZA RECHAZADA',
        'DESCONOCIDO': '⚪ COLOR DESCONOCIDO'
    };
    return colores[color] || color;
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
        addMonitorMessage(result.mensaje, result.ok ? 'success' : 'error');
    } catch (error) {
        addMonitorMessage('Error de conexión con el PLC', 'error');
    }
}

async function resetContador() {
    try {
        const response = await fetch("/api/reset", {
            method: "POST",
            headers: { "Content-Type": "application/json" }
        });
        const result = await response.json();
        addMonitorMessage(result.mensaje, result.ok ? 'success' : 'error');
    } catch (error) {
        addMonitorMessage('Error al resetear encoder', 'error');
    }
}

async function resetContadores() {
    try {
        const response = await fetch("/api/reset_counters", {
            method: "POST",
            headers: { "Content-Type": "application/json" }
        });
        const result = await response.json();
        if (result.ok) {
            document.getElementById("piezas-buenas").innerText = "0";
            document.getElementById("piezas-malas").innerText = "0";
            document.getElementById("piezas-total").innerText = "0";
            document.getElementById("calidad-porcentaje").innerText = "0";
            addMonitorMessage("Contadores de producción reiniciados", 'success');
        }
    } catch (error) {
        addMonitorMessage('Error al reiniciar contadores', 'error');
    }
}

async function setReferencia() {
    const input = document.getElementById("referencia-input");
    const valor = parseFloat(input.value);
    
    if (isNaN(valor)) {
        addMonitorMessage('Ingrese un valor de RPM válido', 'error');
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
    const icons = { success: '✅', error: '❌', info: '📡', system: '🏭' };
    const icon = icons[type] || '📡';
    
    const line = document.createElement('div');
    line.className = `monitor-line ${type}`;
    line.innerHTML = `<span class="monitor-time">[${timestamp}]</span> ${icon} ${message}`;
    
    monitor.appendChild(line);
    line.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
    
    while (monitor.children.length > 150) {
        monitor.removeChild(monitor.firstChild);
    }
}

function clearMonitor() {
    const monitor = document.getElementById("monitor");
    if (monitor) {
        monitor.innerHTML = '<div class="monitor-line system">[🏭] Monitor limpiado</div>';
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
                    borderColor: '#06b6d4',
                    backgroundColor: 'rgba(6, 182, 212, 0.05)',
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
                    borderWidth: 2.5,
                    borderDash: [8, 4],
                    pointRadius: 0,
                    tension: 0.3,
                    fill: false
                }
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: true,
            interaction: { mode: 'index', intersect: false },
            plugins: {
                legend: { 
                    position: 'top', 
                    labels: { color: '#f3f4f6', font: { size: 11, weight: '600' }, usePointStyle: true } 
                },
                tooltip: { 
                    mode: 'index', 
                    intersect: false, 
                    backgroundColor: '#1f2937', 
                    titleColor: '#f3f4f6', 
                    bodyColor: '#9ca3af', 
                    borderColor: '#06b6d4', 
                    borderWidth: 1,
                    callbacks: {
                        label: (ctx) => `${ctx.dataset.label}: ${ctx.parsed.y.toFixed(1)} RPM`
                    }
                }
            },
            scales: {
                x: { 
                    grid: { color: '#1f2937', display: false }, 
                    ticks: { color: '#6b7280', maxTicksLimit: 6 },
                    title: { display: true, text: 'Tiempo (muestras)', color: '#6b7280', font: { size: 10 } }
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
    
    if (rpmHistory.length > 50) {
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
        'VERDE': { msg: '✅ ESTADO ÓPTIMO - Error < 5%', color: 'verde' },
        'AMARILLO': { msg: '⚠️ MARGEN DE ERROR - Error 5-15%', color: 'amarillo' },
        'ROJO': { msg: '🔴 FUERA DE ESPECIFICACIÓN - Error > 15%', color: 'rojo' }
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
    
    // Normalizar valores (0-255)
    const rNorm = Math.min(100, (rojo / 100) * 100);
    const gNorm = Math.min(100, (verde / 100) * 100);
    const bNorm = Math.min(100, (azul / 100) * 100);
    
    if (rojoBar) rojoBar.style.width = rNorm + '%';
    if (verdeBar) verdeBar.style.width = gNorm + '%';
    if (azulBar) azulBar.style.width = bNorm + '%';
    
    if (rojoValor) rojoValor.innerText = rojo;
    if (verdeValor) verdeValor.innerText = verde;
    if (azulValor) azulValor.innerText = azul;
    
    if (colorDetectado) {
        colorDetectado.innerText = getColorDisplay(color);
        if (color === 'VERDE') {
            colorPreview.style.background = '#10b981';
            colorPreview.style.boxShadow = '0 0 20px #10b981';
            addMonitorMessage(`🎨 Pieza VERDE detectada - Producto apto`, 'success');
        } else if (color === 'LADRILLO') {
            colorPreview.style.background = '#cd5c5c';
            colorPreview.style.boxShadow = '0 0 20px #cd5c5c';
            addMonitorMessage(`🔴 PIEZA RECHAZADA - Color LADRILLO detectado`, 'error');
        } else {
            colorPreview.style.background = '#6b7280';
            colorPreview.style.boxShadow = 'none';
        }
    }
}

function updatePIDInfo(pwm, error, integral) {
    const pwmBar = document.getElementById('pwm-bar');
    const pwmValue = document.getElementById('pwm-value');
    const errorValue = document.getElementById('error-value');
    const integralValue = document.getElementById('integral-value');
    
    if (pwmBar) pwmBar.style.width = (pwm / 255) * 100 + '%';
    if (pwmValue) pwmValue.innerText = pwm;
    if (errorValue) {
        errorValue.innerText = error.toFixed(1);
        if (Math.abs(error) <= 5) errorValue.style.color = '#10b981';
        else if (Math.abs(error) <= 15) errorValue.style.color = '#f59e0b';
        else errorValue.style.color = '#ef4444';
    }
    if (integralValue) integralValue.innerText = integral.toFixed(1);
}

function updateCounters(buenas, malas) {
    const total = buenas + malas;
    const calidad = total > 0 ? (buenas / total * 100).toFixed(1) : 0;
    
    document.getElementById("piezas-buenas").innerText = buenas;
    document.getElementById("piezas-malas").innerText = malas;
    document.getElementById("piezas-total").innerText = total;
    document.getElementById("calidad-porcentaje").innerText = calidad;
    
    const qualityIndicator = document.getElementById("quality-indicator");
    if (qualityIndicator) {
        if (calidad >= 90) qualityIndicator.style.borderLeftColor = '#10b981';
        else if (calidad >= 70) qualityIndicator.style.borderLeftColor = '#f59e0b';
        else qualityIndicator.style.borderLeftColor = '#ef4444';
    }
}

function updateKPI(id, value, suffix = '') {
    const element = document.getElementById(`${id}-value`);
    if (!element) return;
    const newValue = `${value}${suffix}`;
    if (element.innerText !== newValue) {
        element.style.transform = 'scale(1.05)';
        element.innerText = newValue;
        setTimeout(() => { if (element) element.style.transform = ''; }, 200);
    }
}

async function actualizarDatos() {
    if (isUpdating) return;
    isUpdating = true;
    
    try {
        const response = await fetch("/api/data");
        const data = await response.json();
        
        if (data.ok) {
            updateKPI('rpm', data.rpm.toFixed(0));
            updateKPI('estado', getEstadoDisplay(data.estado));
            updateKPI('pulsos', data.pulsos.toLocaleString());
            updateKPI('referencia', data.referencia.toFixed(0));
            updateKPI('error', data.error.toFixed(1));
            updateKPI('pwm', data.pwm);
            updateKPI('integral', data.integral.toFixed(1));
            
            if (data.referencia !== undefined && document.getElementById("referencia-value")) {
                document.getElementById("referencia-value").innerText = data.referencia.toFixed(0);
            }
            
            updateSemaforo(data.semaforo);
            updatePIDInfo(data.pwm, data.error, data.integral);
            updateSensorColor(data.color, data.rojo, data.verde, data.azul);
            updateCounters(data.piezas_buenas, data.piezas_malas);
            updateCharts(data.rpm, data.referencia);
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
    setInterval(actualizarDatos, 250);
    
    const refInput = document.getElementById("referencia-input");
    if (refInput) {
        refInput.addEventListener("keypress", (e) => {
            if (e.key === "Enter") setReferencia();
        });
    }
    
    addMonitorMessage('Sistema Industrial Color Sorter iniciado correctamente', 'system');
    addMonitorMessage('Control PID activo - Esperando detección de piezas', 'system');
});

let resizeTimeout;
window.addEventListener('resize', () => {
    clearTimeout(resizeTimeout);
    resizeTimeout = setTimeout(() => rpmChart?.resize(), 250);
});