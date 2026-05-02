let rpmChart = null;
let rpmHistory = [];
let referenciaHistory = [];
let isUpdating = false;

function getEstadoDisplay(estado) {
    const nombres = {
        'DERECHA': 'ADELANTE',
        'IZQUIERDA': 'REVERSA',
        'PARO': 'PARO'
    };
    return nombres[estado] || estado;
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
        addMonitorMessage('Error de conexión', 'error');
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
        addMonitorMessage('Error al resetear', 'error');
    }
}

async function setReferencia() {
    const input = document.getElementById("referencia-input");
    const valor = parseFloat(input.value);
    
    if (isNaN(valor)) {
        addMonitorMessage('Ingrese un valor válido', 'error');
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
    const colors = { success: '#10b981', error: '#ef4444', info: '#06b6d4' };
    
    const line = document.createElement('div');
    line.className = 'monitor-line';
    line.style.color = colors[type];
    line.innerHTML = `<span class="monitor-time">[${timestamp}]</span> ${message}`;
    
    monitor.appendChild(line);
    line.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
    
    while (monitor.children.length > 100) {
        monitor.removeChild(monitor.firstChild);
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
                    pointHoverRadius: 5,
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
                legend: { position: 'top', labels: { color: '#f3f4f6', font: { size: 10, weight: '500' }, boxWidth: 10 } },
                tooltip: { mode: 'index', intersect: false, backgroundColor: '#1f2937', titleColor: '#f3f4f6', bodyColor: '#9ca3af', borderColor: '#06b6d4', borderWidth: 1 }
            },
            scales: {
                x: { grid: { color: '#1f2937', display: false }, ticks: { color: '#6b7280', maxTicksLimit: 6 } },
                y: { grid: { color: '#1f2937' }, ticks: { color: '#6b7280', callback: (v) => `${v}` }, beginAtZero: true }
            }
        }
    });
}

function updateCharts(rpm, referencia) {
    if (!rpmChart) return;
    
    rpmHistory.push(rpm);
    referenciaHistory.push(referencia);
    
    if (rpmHistory.length > 40) {
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
        'VERDE': { msg: '✅ Cerca de referencia', color: 'verde' },
        'AMARILLO': { msg: '⚠️ Medio alejado', color: 'amarillo' },
        'ROJO': { msg: '🔴 Lejos de referencia', color: 'rojo' }
    };
    
    const estado = mensajes[semaforo] || mensajes['ROJO'];
    semaforoLeds.classList.add(estado.color);
    if (semaforoMessage) semaforoMessage.innerText = estado.msg;
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
            updateKPI('vueltas', data.vueltas.toFixed(3));
            updateKPI('error_pct', data.error_pct.toFixed(1));
            updateKPI('pwm', data.pwm);
            
            if (data.referencia !== undefined) {
                document.getElementById("referencia-value").innerText = data.referencia.toFixed(0);
            }
            
            updateSemaforo(data.semaforo);
            updatePIDInfo(data.pwm, data.error, data.integral);
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
    setInterval(actualizarDatos, 300);
    
    const refInput = document.getElementById("referencia-input");
    if (refInput) {
        refInput.addEventListener("keypress", (e) => {
            if (e.key === "Enter") setReferencia();
        });
    }
});

let resizeTimeout;
window.addEventListener('resize', () => {
    clearTimeout(resizeTimeout);
    resizeTimeout = setTimeout(() => rpmChart?.resize(), 250);
});