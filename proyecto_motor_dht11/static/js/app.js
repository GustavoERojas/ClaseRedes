// Elementos del DOM
const tempElement = document.getElementById("temp");
const humElement = document.getElementById("hum");
const rangoElement = document.getElementById("rango");
const pwmElement = document.getElementById("pwm");
const pwmFill = document.getElementById("pwmFill");
const humidityPercent = document.getElementById("humidityPercent");
const motorPercent = document.getElementById("motorPercent");
const efficiencyPercent = document.getElementById("efficiencyPercent");
const connectionDot = document.getElementById("connectionDot");
const connectionText = document.getElementById("connectionText");

// LEDs
const ledSeco = document.getElementById("ledSeco");
const ledModerado = document.getElementById("ledModerado");
const ledHumedo = document.getElementById("ledHumedo");
const ledMotor = document.getElementById("ledMotor");
const ledMotorStatus = document.getElementById("ledMotorStatus");

// Configuración de la gráfica
let chart;
let chartData = [];
let maxDataPoints = 30;
let lastValues = { temp: 0, hum: 0, pwm: 0 };

// Inicializar gráfica
function initChart() {
    const ctx = document.getElementById('realtimeChart').getContext('2d');
    chart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: [],
            datasets: [
                {
                    label: 'Temperatura (°C)',
                    data: [],
                    borderColor: '#f59e0b',
                    backgroundColor: 'rgba(245, 158, 11, 0.1)',
                    tension: 0.4,
                    fill: true,
                    pointRadius: 3,
                    pointHoverRadius: 6
                },
                {
                    label: 'Humedad (%)',
                    data: [],
                    borderColor: '#3b82f6',
                    backgroundColor: 'rgba(59, 130, 246, 0.1)',
                    tension: 0.4,
                    fill: true,
                    pointRadius: 3,
                    pointHoverRadius: 6
                },
                {
                    label: 'Velocidad PWM (%)',
                    data: [],
                    borderColor: '#10b981',
                    backgroundColor: 'rgba(16, 185, 129, 0.1)',
                    tension: 0.4,
                    fill: true,
                    pointRadius: 3,
                    pointHoverRadius: 6
                }
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: true,
            plugins: {
                legend: {
                    position: 'top',
                    labels: {
                        color: 'white',
                        font: { size: 12 }
                    }
                },
                tooltip: {
                    mode: 'index',
                    intersect: false,
                    backgroundColor: 'rgba(0,0,0,0.8)',
                    titleColor: 'white',
                    bodyColor: 'rgba(255,255,255,0.8)'
                }
            },
            scales: {
                y: {
                    grid: { color: 'rgba(255,255,255,0.1)' },
                    ticks: { color: 'white' }
                },
                x: {
                    grid: { color: 'rgba(255,255,255,0.1)' },
                    ticks: { color: 'white' }
                }
            },
            animation: {
                duration: 500
            }
        }
    });
}

// Actualizar LEDs según rango
function updateLEDs(rango, pwm) {
    // Apagar todos
    ledSeco.className = 'led-light led-off';
    ledModerado.className = 'led-light led-off';
    ledHumedo.className = 'led-light led-off';
    
    // Encender según rango
    switch(rango.toUpperCase()) {
        case 'SECO':
            ledSeco.className = 'led-light led-seco active';
            break;
        case 'MODERADO':
            ledModerado.className = 'led-light led-moderado active';
            break;
        case 'HUMEDO':
            ledHumedo.className = 'led-light led-humedo active';
            break;
    }
    
    // Actualizar LED del motor
    if (pwm > 0) {
        const intensity = Math.min(1, pwm / 255);
        const brightness = Math.floor(intensity * 100);
        ledMotor.className = 'led-light led-motor active';
        ledMotor.style.opacity = 0.3 + intensity * 0.7;
        ledMotorStatus.textContent = `Funcionando (${brightness}%)`;
    } else {
        ledMotor.className = 'led-light led-off';
        ledMotor.style.opacity = 1;
        ledMotorStatus.textContent = 'Detenido';
    }
}

// Actualizar círculos de progreso
const circumference = 2 * Math.PI * 45;

function updateCircleProgress(circleElement, percentage) {
    const circle = circleElement.querySelector('.fill');
    if (!circle) return;
    
    const dashOffset = circumference - (percentage / 100) * circumference;
    circle.style.strokeDasharray = `${circumference} ${circumference}`;
    circle.style.strokeDashoffset = dashOffset;
    
    // Cambiar color según porcentaje
    if (percentage < 40) {
        circle.style.stroke = '#f59e0b';
    } else if (percentage < 70) {
        circle.style.stroke = '#3b82f6';
    } else {
        circle.style.stroke = '#10b981';
    }
}

// Actualizar gráfica
function updateChart(temp, hum, pwm) {
    const time = new Date().toLocaleTimeString();
    const pwmPercent = (pwm / 255) * 100;
    
    chartData.push({
        time: time,
        temp: temp,
        hum: hum,
        pwm: pwmPercent
    });
    
    // Mantener solo los últimos puntos
    if (chartData.length > maxDataPoints) {
        chartData.shift();
    }
    
    // Actualizar gráfica
    chart.data.labels = chartData.map(d => d.time);
    chart.data.datasets[0].data = chartData.map(d => d.temp);
    chart.data.datasets[1].data = chartData.map(d => d.hum);
    chart.data.datasets[2].data = chartData.map(d => d.pwm);
    chart.update('none');
}

// Control de rango de gráfica
function setChartRange(seconds) {
    maxDataPoints = seconds / 2; // Cada 2 segundos un punto
    const pointsToKeep = Math.min(maxDataPoints, chartData.length);
    chartData = chartData.slice(-pointsToKeep);
    updateChart(lastValues.temp, lastValues.hum, lastValues.pwm);
    
    // Actualizar botones activos
    document.querySelectorAll('.chart-btn').forEach(btn => {
        btn.classList.remove('active');
    });
    event.target.classList.add('active');
}

// Limpiar gráfica
function clearChart() {
    chartData = [];
    updateChart(0, 0, 0);
}

// Actualizar estado de conexión
let errorCount = 0;

function updateConnectionStatus(connected) {
    if (connected) {
        connectionDot.className = 'connection-dot';
        connectionText.textContent = 'Conectado en tiempo real';
        errorCount = 0;
    } else {
        connectionDot.className = 'connection-dot error';
        connectionText.textContent = 'Error de conexión - Reintentando...';
    }
}

// Obtener datos del servidor
async function getData() {
    try {
        const res = await fetch("/api/data");
        const data = await res.json();

        if (data.temp !== undefined && data.temp !== null) {
            // Actualizar valores
            const temp = data.temp.toFixed(1);
            const hum = data.hum.toFixed(1);
            const pwm = data.pwm;
            const rango = data.rango;
            
            tempElement.textContent = temp;
            humElement.textContent = hum;
            rangoElement.textContent = rango;
            pwmElement.textContent = pwm;
            
            // Actualizar barra de PWM
            const pwmPercent = (pwm / 255) * 100;
            pwmFill.style.width = pwmPercent + "%";
            pwmFill.textContent = Math.round(pwmPercent) + "%";
            
            // Actualizar círculos
            const humidityCircle = document.getElementById('humidityCircle');
            const motorCircle = document.getElementById('motorCircle');
            const efficiencyCircle = document.getElementById('efficiencyCircle');
            
            if (humidityCircle) {
                updateCircleProgress(humidityCircle, hum);
                humidityPercent.textContent = Math.round(hum) + "%";
            }
            
            if (motorCircle) {
                updateCircleProgress(motorCircle, pwmPercent);
                motorPercent.textContent = Math.round(pwmPercent) + "%";
            }
            
            if (efficiencyCircle) {
                // Eficiencia basada en relación humedad/velocidad óptima
                const efficiency = Math.min(100, (pwmPercent / hum) * 100);
                updateCircleProgress(efficiencyCircle, efficiency);
                efficiencyPercent.textContent = Math.round(efficiency) + "%";
            }
            
            // Actualizar LEDs
            updateLEDs(rango, pwm);
            
            // Actualizar gráfica
            lastValues = { temp: parseFloat(temp), hum: parseFloat(hum), pwm: pwm };
            updateChart(parseFloat(temp), parseFloat(hum), pwm);
            
            // Actualizar estado de conexión
            updateConnectionStatus(true);
            errorCount = 0;
            
        } else if (data.error) {
            errorCount++;
            if (errorCount > 3) {
                updateConnectionStatus(false);
            }
            console.log("Error:", data.error);
        }
    } catch (e) {
        errorCount++;
        if (errorCount > 3) {
            updateConnectionStatus(false);
        }
        console.log("Error de conexión:", e);
    }
}

// Inicializar todo
document.addEventListener('DOMContentLoaded', () => {
    initChart();
    
    // Animación de entrada
    const cards = document.querySelectorAll('.card, .indicator, .chart-container');
    cards.forEach((card, index) => {
        card.style.animationDelay = `${index * 0.05}s`;
    });
    
    // Inicializar círculos
    const circles = document.querySelectorAll('.fill');
    circles.forEach(circle => {
        circle.style.strokeDasharray = `${circumference} ${circumference}`;
        circle.style.strokeDashoffset = circumference;
    });
});

// Actualizar cada 2 segundos
setInterval(getData, 2000);
getData();