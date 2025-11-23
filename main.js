let wasmModule = null;
let evalFunc = null;
let getCollections = null;
let getAllocated = null;
let getFreed = null;
let getCurrent = null;
let checkInput = null;

let isRunning = false;
let intervalId = null;

// Charts
let memoryChart = null;
let collectionsChart = null;

// Initialize WASM
Module.onRuntimeInitialized = function () {
    wasmModule = Module;
    evalFunc = Module.cwrap('eval', 'string', ['string']);
    getCollections = Module.cwrap('gc_get_collections_count', 'number', []);
    getAllocated = Module.cwrap('gc_get_allocated_bytes', 'number', []);
    getFreed = Module.cwrap('gc_get_freed_bytes', 'number', []);
    getCurrent = Module.cwrap('gc_get_current_bytes', 'number', []);
    checkInput = Module.cwrap('form_needs_more_input', 'number', ['string']);

    document.getElementById('status').textContent = 'Status: WASM Loaded';
    initCharts();
    updateStats();
};

function initCharts() {
    const ctxMem = document.getElementById('memoryChart').getContext('2d');
    memoryChart = new Chart(ctxMem, {
        type: 'line',
        data: {
            labels: [],
            datasets: [{
                label: 'Current Bytes',
                data: [],
                borderColor: 'rgb(75, 192, 192)',
                tension: 0.1
            }, {
                label: 'Total Allocated',
                data: [],
                borderColor: 'rgb(255, 99, 132)',
                tension: 0.1
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            animation: false,
            scales: {
                y: { beginAtZero: true }
            }
        }
    });

    const ctxColl = document.getElementById('collectionsChart').getContext('2d');
    collectionsChart = new Chart(ctxColl, {
        type: 'bar',
        data: {
            labels: ['Collections'],
            datasets: [{
                label: 'GC Cycles',
                data: [0],
                backgroundColor: 'rgb(54, 162, 235)'
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            scales: {
                y: { beginAtZero: true, ticks: { stepSize: 1 } }
            }
        }
    });
}

function updateStats() {
    if (!wasmModule) return;

    const current = getCurrent();
    const allocated = getAllocated();
    const collections = getCollections();

    // Update Memory Chart
    const now = new Date().toLocaleTimeString();
    if (memoryChart.data.labels.length > 50) {
        memoryChart.data.labels.shift();
        memoryChart.data.datasets[0].data.shift();
        memoryChart.data.datasets[1].data.shift();
    }
    memoryChart.data.labels.push(now);
    memoryChart.data.datasets[0].data.push(current);
    memoryChart.data.datasets[1].data.push(allocated);
    memoryChart.update();

    // Update Collections Chart
    collectionsChart.data.datasets[0].data[0] = collections;
    collectionsChart.update();
}

// Allocation loop script
const allocScript = `
(define (make-garbage n)
  (if (= n 0)
      nil
      (cons n (make-garbage (- n 1)))))
(make-garbage 100)
`;

function startLoop() {
    if (isRunning) return;
    isRunning = true;
    document.getElementById('startBtn').disabled = true;
    document.getElementById('stopBtn').disabled = false;
    document.getElementById('status').textContent = 'Status: Running...';

    intervalId = setInterval(() => {
        try {
            evalFunc(allocScript);
            updateStats();
        } catch (e) {
            console.error(e);
            stopLoop();
        }
    }, 100); // Run every 100ms
}

function stopLoop() {
    if (!isRunning) return;
    isRunning = false;
    clearInterval(intervalId);
    document.getElementById('startBtn').disabled = false;
    document.getElementById('stopBtn').disabled = true;
    document.getElementById('status').textContent = 'Status: Stopped';
}

document.getElementById('startBtn').addEventListener('click', startLoop);
document.getElementById('stopBtn').addEventListener('click', stopLoop);
document.getElementById('gcBtn').addEventListener('click', () => {
    if (evalFunc) {
        evalFunc('(gc)');
        updateStats();
        logOutput('Forced GC');
    }
});

// REPL Logic
const terminal = document.getElementById('terminal');
const commandInput = document.getElementById('commandInput');

function logOutput(text, isError = false) {
    const line = document.createElement('div');
    line.textContent = text;
    if (isError) line.style.color = '#ff6b6b';
    terminal.appendChild(line);
    terminal.scrollTop = terminal.scrollHeight;
}

let commandBuffer = '';

function runCommand() {
    if (!evalFunc) return;
    const cmd = commandInput.value; // Don't trim immediately to preserve trailing spaces if needed, though usually fine.
    if (!cmd && !commandBuffer) return;

    if (commandBuffer) {
        logOutput('... ' + cmd);
        commandBuffer += '\n' + cmd;
    } else {
        logOutput('> ' + cmd);
        commandBuffer = cmd;
    }

    commandInput.value = '';

    if (checkInput(commandBuffer)) {
        commandInput.placeholder = '...';
        return;
    }

    try {
        const result = evalFunc(commandBuffer);
        logOutput(result);
        updateStats(); // Update charts immediately
    } catch (e) {
        logOutput('Error: ' + e, true);
    }
    commandBuffer = '';
    commandInput.placeholder = '(cons 1 2)';
}

document.getElementById('runBtn').addEventListener('click', runCommand);
commandInput.addEventListener('keypress', (e) => {
    if (e.key === 'Enter') runCommand();
});
