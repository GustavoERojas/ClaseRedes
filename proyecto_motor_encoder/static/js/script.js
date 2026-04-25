let rpmChart = null;
let rpmHistory = [];
let isUpdating = false;

function getCommandIcon(cmd) {
    const icons = {
        'A': '▶',
        'R': '◀',
        'P': '■',
        'Z': '↺'
    };
    return icons[cmd] || '●';
}

function getCommandName(cmd) {
    const names = {
        'A': 'Adelante',
        'R': 'Reversa',
        'P': 'Paro',
        'Z': 'Reset'
    };
    return names[cmd] || cmd;
}

async function enviarComando(cmd) {
    const btn = event?.target?.closest('.ctrl-btn');
    if (btn) {
        btn.style.transform = 'scale(0.95)';
        setTimeout(() => {
            if (btn) btn.style.transform = '';
        }, 150);
    }
    
    try {
        const response = await fetch("/control", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ comando: cmd })
        });
        
        const data = await response.json();
        
        if (data.ok) {
            addMonitorMessage(`Comando ${getCommandName(cmd)} enviado`, 'success');
        } else {
            addMonitorMessage(`Error: ${data.error}`, 'error');
        }
    } catch (error) {
        addMonitorMessage('Error de conexión', 'error');
    }
}

function addMonitorMessage(message, type = 'info') {
    const monitor = document.getElementById("monitor");
    if (!monitor) return;
    
    const timestamp = new Date().toLocaleTimeString();
    const colors = {
        success: '#10b981',
        error: '#ef4444',
        info: '#06b6d4'
    };
    
    const line = document.createElement('div');
    line.className = 'monitor-line';
    line.style.color = colors[type];
    line.innerHTML = `<span style="color:#6b7280">[${timestamp}]</span> ${message}`;
    
    monitor.appendChild(line);
    line.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
    
    // Limitar líneas
    while (monitor.children.length > 100) {
        monitor.removeChild(monitor.firstChild);
    }
}

function initChart() {
    const canvas = document.getElementById('rpmChart');
    if (!canvas) return;
    
    const ctx = canvas.getContext('2d');
    
    if (rpmChart) rpmChart.destroy();
    
    rpmChart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: [],
            datasets: [{
                label: 'RPM',
                data: [],
                borderColor: '#06b6d4',
                backgroundColor: 'rgba(6, 182, 212, 0.05)',
                borderWidth: 2,
                pointRadius: 0,
                pointHoverRadius: 6,
                pointHoverBackgroundColor: '#06b6d4',
                tension: 0.4,
                fill: true
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: true,
            plugins: {
                legend: { display: false },
                tooltip: {
                    mode: 'index',
                    intersect: false,
                    backgroundColor: '#1f2937',
                    titleColor: '#f3f4f6',
                    bodyColor: '#9ca3af',
                    borderColor: '#06b6d4',
                    borderWidth: 1,
                    callbacks: {
                        label: (ctx) => `RPM: ${ctx.parsed.y.toFixed(1)}`
                    }
                }
            },
            scales: {
                x: {
                    grid: { color: '#374151', display: false },
                    ticks: { color: '#9ca3af', maxTicksLimit: 8 }
                },
                y: {
                    grid: { color: '#374151' },
                    ticks: { color: '#9ca3af', callback: (v) => `${v} RPM` },
                    beginAtZero: true,
                    title: {
                        display: true,
                        text: 'Revoluciones por Minuto',
                        color: '#9ca3af',
                        font: { size: 11 }
                    }
                }
            },
            interaction: { mode: 'index', intersect: false }
        }
    });
}

function updateRPMChart(rpm) {
    if (!rpmChart) return;
    
    rpmHistory.push(rpm);
    if (rpmHistory.length > 50) rpmHistory.shift();
    
    const labels = rpmHistory.map((_, i) => i);
    rpmChart.data.labels = labels;
    rpmChart.data.datasets[0].data = [...rpmHistory];
    rpmChart.update('none');
}

async function actualizarDatos() {
    if (isUpdating) return;
    isUpdating = true;
    
    try {
        const response = await fetch("/api/data");
        const data = await response.json();
        
        if (data.ok) {
            // Actualizar KPI con animación
            updateKPI('cmd', data.cmd || 'P', getCommandIcon(data.cmd));
            updateKPI('pulsos', (data.pulsos || 0).toLocaleString());
            updateKPI('vueltas', (data.vueltas || 0).toFixed(4));
            updateKPI('rpm', (data.rpm || 0).toFixed(1), '', 'RPM');
            
            // Actualizar gráfico
            updateRPMChart(data.rpm || 0);
            
            // Cambiar color según RPM
            const rpmValue = document.getElementById('rpm-value');
            if (rpmValue) {
                const rpm = data.rpm || 0;
                if (rpm > 500) rpmValue.style.color = '#ef4444';
                else if (rpm > 200) rpmValue.style.color = '#f59e0b';
                else if (rpm > 0) rpmValue.style.color = '#10b981';
                else rpmValue.style.color = '#f3f4f6';
            }
        }
    } catch (error) {
        console.error('Error:', error);
    } finally {
        isUpdating = false;
    }
}

function updateKPI(id, value, prefix = '', suffix = '') {
    const element = document.getElementById(`${id}-value`);
    if (!element) return;
    
    const oldValue = element.innerText;
    if (oldValue !== value) {
        element.style.transform = 'scale(1.05)';
        element.innerText = `${prefix}${value}${suffix}`;
        setTimeout(() => {
            if (element) element.style.transform = '';
        }, 200);
    } else {
        element.innerText = `${prefix}${value}${suffix}`;
    }
}

// Inicializar
document.addEventListener('DOMContentLoaded', () => {
    initChart();
    actualizarDatos();
    setInterval(actualizarDatos, 300);
});

// Redimensionar gráfico
let resizeTimeout;
window.addEventListener('resize', () => {
    clearTimeout(resizeTimeout);
    resizeTimeout = setTimeout(() => rpmChart?.resize(), 250);
});