// htmlPage_S.h
const char* htmlPage_S = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Настройки коэффициентов</title>     
    <style>
        body {
            font-family: Arial, sans-serif;
            text-align: center;
            margin: 0;
            padding: 20px;
            background-color: #f5f5f5;
        }
        h1 {
            color: #333;
        }
        .container {
            display: flex;
            flex-direction: column;
            align-items: center;
            gap: 20px;
            margin-top: 20px;
        }
        .settings-panel {
            background-color: white;
            border-radius: 10px;
            padding: 20px;
            box-shadow: 0 4px 8px rgba(0,0,0,0.1);
            width: 90%;
            max-width: 900px;
        }
        .header-row {
            display: flex;
            justify-content: space-between;
            padding: 10px 0;
            font-weight: bold;
            border-bottom: 1px solid #ddd;
        }
        .channel-row {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 10px 0;
            border-bottom: 1px solid #eee;
        }
        .channel-info {
            display: flex;
            align-items: center;
            gap: 10px;
            width: 60%;
        }
        .channel-number {
            font-weight: bold;
            width: 40px;
            text-align: center;
        }
        .voltage-display {
            width: 100px;
            text-align: right;
            font-family: monospace;
        }
        .column-title {
            width: 100px;
            text-align: center;
            font-weight: bold;
        }
        input[type="number"] {
            width: 80px;
            padding: 8px;
            border: 1px solid #ddd;
            border-radius: 4px;
            text-align: right;
            font-size: 14px;
        }
        .buttons {
            margin-top: 20px;
            display: flex;
            justify-content: center;
            gap: 10px;
        }
        button {
            padding: 10px 20px;
            background-color: #3498db;
            color: white;
            border: none;
            border-radius: 5px;
            cursor: pointer;
            font-size: 16px;
        }
        button:hover {
            background-color: #2980b9;
        }
        button.save {
            background-color: #27ae60;
        }
        button.save:hover {
            background-color: #219653;
        }
        button.reset {
            background-color: #e74c3c;
        }
        button.reset:hover {
            background-color: #c0392b;
        }
        .update-info {
            margin-top: 20px;
            color: #7f8c8d;
            font-size: 14px;
        }
        .status-message {
            margin-top: 10px;
            padding: 10px;
            border-radius: 5px;
            display: none;
        }
        .success {
            background-color: #d5f5e3;
            color: #27ae60;
            display: block;
        }
        .error {
            background-color: #fadbd8;
            color: #e74c3c;
            display: block;
        }
        .code-output {
            margin-top: 30px;
            padding: 15px;
            background-color: #f0f0f0;
            border-radius: 5px;
            font-family: monospace;
            white-space: pre;
            text-align: left;
            max-width: 600px;
            margin-left: auto;
            margin-right: auto;
        }
        .target-voltage {
            background-color: #f0f8ff;
            border: 1px solid #3498db;
        }
        .save-btn {
            padding: 8px 12px;
            background-color: #27ae60;
            margin-left: 10px;
        }
        .input-group {
            display: flex;
            align-items: center;
            width: 40%;
            justify-content: flex-end;
        }
        .raw-value {
            font-family: monospace;
            width: 100px;
            text-align: right;
        }
        .actions-column {
            width: 100px;
            text-align: center;
        }
    </style>
</head>
<body>
    <h1>Настройки коэффициентов делителя напряжения</h1>
    <p>Коэффициенты сохраняются только при нажатии кнопки "Записать"</p>
    
    <div class="container">
        <div class="settings-panel">
            <div class="header-row">
                <div class="channel-number">Канал</div>
                <div class="column-title">Сырое АЦП</div>
                <div class="column-title">Измеренное (В)</div>
                <div class="column-title">Целевое (В)</div>
                <div class="column-title">Коэффициент</div>
                <div class="actions-column">Действия</div>
            </div>
            <div id="channels-container">
                <!-- Каналы будут созданы скриптом -->
            </div>
            
            <div class="buttons">
                <button onclick="window.location.href='/'">На главную</button>
            </div>
        </div>
        
        <div class="update-info" id="updateInfo">Загрузка данных...</div>
        <div class="status-message" id="statusMessage"></div>
        
        <div class="code-output" id="codeOutput"></div>
    </div>

    <script>
        // Текущие коэффициенты (будут загружены с сервера)
        let coefficients = Array(13).fill(12.0000);
        let pendingCoefficients = [...coefficients];
        let targetVoltages = Array(13).fill(0);
        let rawADCValues = Array(13).fill(0);

        // Загрузка коэффициентов с сервера
        async function loadCoefficients() {
            try {
                const response = await fetch('/getCoefficients');
                if (!response.ok) throw new Error('Ошибка загрузки');
                
                const data = await response.json();
                if (data && data.length === 13) {
                    coefficients = data.map(val => parseFloat(val.toFixed(4)));
                    pendingCoefficients = [...coefficients];
                    updateCodeOutput();
                    createChannelElements();
                    document.getElementById('updateInfo').textContent = 'Данные загружены';
                }
            } catch (error) {
                console.error('Ошибка:', error);
                document.getElementById('updateInfo').textContent = 'Ошибка загрузки коэффициентов';
            }
        }

        function toFixed(value, precision) {
            return parseFloat(value.toFixed(precision));
        }
        
        function updateCodeOutput() {
            let code = "float voltageDividerRatios[13] = {\n";
            coefficients.forEach((coeff, i) => {
                code += `  ${toFixed(coeff, 4)}${i < 12 ? ',' : ''}${i === 5 ? '\n' : ''}\n`;
            });
            code += "};";
            document.getElementById("codeOutput").textContent = code;
        }
        
        function createChannelElements() {
            const container = document.getElementById('channels-container');
            container.innerHTML = '';
            
            coefficients.forEach((coeff, i) => {
                container.innerHTML += `
                    <div class="channel-row">
                        <div class="channel-info">
                            <div class="channel-number">C${i+1}</div>
                            <div class="raw-value" id="raw-adc-${i}">0</div>
                            <div class="raw-value" id="voltage-${i}">0.0000 В</div>
                            <div>
                                <input type="number" step="0.0001" min="0" max="100" 
                                       id="target-${i}" class="target-voltage" 
                                       placeholder="Цель (В)" 
                                       oninput="calculateCoefficient(${i})">
                            </div>
                        </div>
                        <div class="input-group">
                            <input type="number" step="0.0001" min="1" max="100" 
                                   id="coeff-${i}" value="${toFixed(pendingCoefficients[i], 4)}" 
                                   oninput="updatePendingCoefficient(${i})">
                            <button class="save-btn" onclick="saveCoefficient(${i})">Записать</button>
                        </div>
                    </div>
                `;
            });
        }
        
        function updatePendingCoefficient(channel) {
            const input = document.getElementById(`coeff-${channel}`);
            const newValue = parseFloat(input.value);
            
            if (!isNaN(newValue)) {
                pendingCoefficients[channel] = toFixed(newValue, 4);
                input.value = pendingCoefficients[channel].toFixed(4);
                updateCodeOutput();
                updateCalculatedVoltage(channel);
            }
        }
        
        function calculateCoefficient(channel) {
            const targetInput = document.getElementById(`target-${channel}`);
            const targetValue = parseFloat(targetInput.value);
            
            if (!isNaN(targetValue)) {
                targetVoltages[channel] = targetValue;
                const rawADC = rawADCValues[channel];
                
                if (rawADC > 0) {
                    let measuredVoltage;
                    
                    if (channel === 0) {
                        measuredVoltage = targetValue / ((rawADC / 1023) * 3.3);
                    } else {
                        const prevRawADC = rawADCValues[channel-1] || 1;
                        const prevCoeff = coefficients[channel-1] || 1;
                        const prevVoltage = (prevRawADC / 1023) * 3.3 * prevCoeff;
                        measuredVoltage = (targetValue + prevVoltage) / ((rawADC / 1023) * 3.3);
                    }
                    
                    const newCoefficient = toFixed(measuredVoltage, 4);
                    
                    if (newCoefficient > 0) {
                        pendingCoefficients[channel] = newCoefficient;
                        document.getElementById(`coeff-${channel}`).value = newCoefficient.toFixed(4);
                        updateCodeOutput();
                        updateCalculatedVoltage(channel);
                    }
                }
            }
        }
        
        async function saveCoefficient(channel) {
            const newValue = pendingCoefficients[channel];
            
            try {
                const response = await fetch('/updateCoefficient', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({channel: channel, coefficient: newValue})
                });
                
                const data = await response.json();
                if (data.success) {
                    coefficients[channel] = newValue;
                    showStatusMessage(`Коэффициент C${channel+1} сохранен`, true);
                } else {
                    throw new Error('Сервер вернул ошибку');
                }
            } catch (error) {
                console.error('Ошибка:', error);
                showStatusMessage(`Ошибка сохранения C${channel+1}`, false);
                document.getElementById(`coeff-${channel}`).value = coefficients[channel].toFixed(4);
                pendingCoefficients[channel] = coefficients[channel];
            }
            updateCodeOutput();
        }
        
        function updateCalculatedVoltage(channel) {
            const rawADC = rawADCValues[channel];
            if (rawADC > 0) {
                const measuredVoltage = rawADC * (3.3 / 1023.0);
                let calculatedVoltage;
                
                if (channel === 0) {
                    calculatedVoltage = toFixed(measuredVoltage * pendingCoefficients[channel], 4);
                } else {
                    const prevRawADC = rawADCValues[channel-1] || 0;
                    const prevCoeff = coefficients[channel-1] || 1;
                    const prevVoltage = prevRawADC * (3.3 / 1023.0) * prevCoeff;
                    calculatedVoltage = toFixed(measuredVoltage * pendingCoefficients[channel] - prevVoltage, 4);
                }
                
                document.getElementById(`voltage-${channel}`).textContent = calculatedVoltage.toFixed(4) + ' В';
            }
        }
        
        async function updateBattery(channel) {
            try {
                const response = await fetch(`/data?channel=${channel}`);
                const data = await response.json();
                
                const rawADCElement = document.getElementById(`raw-adc-${channel}`);
                const voltageElement = document.getElementById(`voltage-${channel}`);
                
                if (data.status === "Нет данных") {
                    rawADCElement.textContent = "0";
                    voltageElement.textContent = "0.0000 В";
                    rawADCValues[channel] = 0;
                } else {
                    rawADCValues[channel] = data.rawADC;
                    rawADCElement.textContent = data.rawADC;
                    voltageElement.textContent = data.voltage.toFixed(4) + ' В';
                    
                    if (targetVoltages[channel] > 0) {
                        const measuredVoltage = data.rawVoltages;
                        const newCoefficient = toFixed(measuredVoltage / targetVoltages[channel], 4);
                        
                        if (newCoefficient > 0 && newCoefficient !== pendingCoefficients[channel]) {
                            pendingCoefficients[channel] = newCoefficient;
                            document.getElementById(`coeff-${channel}`).value = newCoefficient.toFixed(4);
                            updateCodeOutput();
                        }
                    }
                }
                updateLastRefreshTime();
            } catch (error) {
                console.error(`Ошибка обновления C${channel+1}:`, error);
            }
        }
        
        function showStatusMessage(message, isSuccess) {
            const element = document.getElementById('statusMessage');
            element.textContent = message;
            element.className = isSuccess ? 'status-message success' : 'status-message error';
            setTimeout(() => element.className = 'status-message', 3000);
        }
        
        function updateLastRefreshTime() {
            document.getElementById('updateInfo').textContent = 
                `Обновлено: ${new Date().toLocaleTimeString()}`;
        }
        
        function updateAllBatteries() {
            for (let i = 0; i < 13; i++) {
                setTimeout(() => updateBattery(i), i * 200);
            }
        }
        
        function startAutoRefresh() {
            updateAllBatteries();
            setTimeout(startAutoRefresh, 1000);
        }
        
        window.onload = function() {
            loadCoefficients();
            startAutoRefresh();
        };
    </script>
</body>
</html>
)rawliteral";