// script.js
document.addEventListener('DOMContentLoaded', function() {
    // --- REFERENCIAS A ELEMENTOS DEL DOM (ACTUALIZADAS Y NUEVAS) ---
    const currentTimeDateEl = document.getElementById('currentTimeDate'); // Para fecha y hora
    const timeSyncStatusEl = document.getElementById('timeSyncStatus');   // Para estado de sincronización
    const timbreStatusEl = document.getElementById('timbreStatus');
    const toggleTimbreBtn = document.getElementById('toggleTimbre');
    const ringNowBtn = document.getElementById('ringNow');
    const activeScheduleSelect = document.getElementById('activeSchedule');
    
    // Campos de Fecha y Hora
    const dayInput = document.getElementById('dayInput');
    const monthInput = document.getElementById('monthInput');
    const yearInput = document.getElementById('yearInput');
    const hourInput = document.getElementById('hourInput');
    const minuteInput = document.getElementById('minuteInput');
    const setTimeBtn = document.getElementById('setTimeBtn');
    const setTimeMessageEl = document.getElementById('setTimeMessage');

    const tabButtons = document.querySelectorAll('.tab-btn');
    const tabContents = document.querySelectorAll('.tab-content');
    
    // Elementos relacionados con los horarios (schedule)
    const scheduleNameInputs = [
        document.getElementById('schedule1Name'),
        document.getElementById('schedule2Name'),
        document.getElementById('schedule3Name')
    ];
    const scheduleTabNameSpans = [ // Para actualizar el nombre en el título de la pestaña
        document.getElementById('schedule1TabName'),
        document.getElementById('schedule2TabName'),
        document.getElementById('schedule3TabName')
    ];
    const bellsContainers = [
        document.getElementById('bellsContainer0'), // Coincide con data-schedule-index
        document.getElementById('bellsContainer1'),
        document.getElementById('bellsContainer2')
    ];
    const addBellBtns = document.querySelectorAll('.add-bell-btn');
    const saveBtns = document.querySelectorAll('.save-btn');

    // --- VARIABLES GLOBALES ---
    let globalScheduleData = { // Almacenará todos los datos del ESP
        schedules: [[], [], []], // Estructura inicial para evitar errores
        scheduleNames: ["Horario 1", "Horario 2", "Horario 3"],
        activeSchedule: 0,
        timbreEnabled: true,
        currentTime: "00:00:00",
        currentDate: "DD/MM/AAAA",
        timeSetSuccessfully: false
    };
    let lastFetchedServerTime = null; // Para calcular la hora localmente
    let timeOffset = 0; // Diferencia entre la hora del servidor y la del cliente
    
    // --- CONFIGURACIÓN INICIAL ---
    setupTabs();
    fetchInitialData(); // Cargar datos al inicio
    setInterval(fetchStatusData, 30000); // Refrescar datos del ESP cada 30 segundos
    setInterval(displayClientTime, 1000); // Actualizar hora en UI cada segundo

    // --- MANEJADORES DE EVENTOS ---
    toggleTimbreBtn.addEventListener('click', toggleTimbre);
    ringNowBtn.addEventListener('click', ringNow);
    activeScheduleSelect.addEventListener('change', setActiveSchedule);
    setTimeBtn.addEventListener('click', setSystemDateTime);
    
    addBellBtns.forEach(btn => {
        btn.addEventListener('click', function() {
            const scheduleIndex = parseInt(this.getAttribute('data-schedule-index'));
            addNewBellUI(scheduleIndex);
        });
    });
    
    saveBtns.forEach(btn => {
        btn.addEventListener('click', function() {
            const scheduleIndex = parseInt(this.getAttribute('data-schedule-index'));
            saveSchedule(scheduleIndex);
        });
    });

    // --- FUNCIONES ---

    function setupTabs() {
        tabButtons.forEach(button => {
            button.addEventListener('click', function() {
                const tabId = this.getAttribute('data-tab');
                tabButtons.forEach(btn => btn.classList.remove('active'));
                tabContents.forEach(content => content.classList.remove('active'));
                this.classList.add('active');
                document.getElementById(tabId).classList.add('active');
            });
        });
    }
    
    function fetchInitialData() {
        fetch('/api/status')
            .then(response => {
                if (!response.ok) {
                    throw new Error(`HTTP error! status: ${response.status}`);
                }
                return response.json();
            })
            .then(data => {
                globalScheduleData = data; // Guardar todos los datos
                // Calcular offset si la hora del servidor es válida
                if (data.timeSetSuccessfully && data.currentTime && data.currentDate) {
                    const [hours, minutes, seconds] = data.currentTime.split(':').map(Number);
                    // Asumimos que currentDate viene como DD-MM-YYYY o similar
                    // Para simplificar, si no tenemos fecha del servidor, no calculamos offset preciso
                    // Esto es una simplificación. Una librería de manejo de fechas sería mejor.
                    // Por ahora, si el servidor da una hora, la tomamos como referencia inicial.
                    lastFetchedServerTime = new Date(); // Hora actual del cliente
                    const serverNow = new Date(); // Crear una nueva fecha para manipular
                    // Si data.currentDate está disponible y es parseable, usarla.
                    // Esto es un placeholder, necesitarías parsear data.currentDate adecuadamente.
                    // serverNow.setHours(hours, minutes, seconds); // Setear solo la hora puede ser problemático con zonas horarias
                                                              // y cambios de día.
                                                              // Es mejor si el servidor envía un timestamp o una fecha ISO.
                                                              // Por ahora, nos basamos en la hora del cliente si el servidor no da una fecha completa y parseable.
                }
                updateFullUI();
            })
            .catch(error => {
                console.error('Error fetching initial data:', error);
                currentTimeDateEl.textContent = "Error al cargar datos.";
                timeSyncStatusEl.textContent = "(Error)";
            });
    }

    function fetchStatusData() { // Solo para refrescar el estado, no necesariamente la hora
        fetch('/api/status')
            .then(response => response.json())
            .then(data => {
                globalScheduleData.timbreEnabled = data.timbreEnabled;
                globalScheduleData.activeSchedule = data.activeSchedule;
                globalScheduleData.scheduleNames = data.scheduleNames;
                globalScheduleData.schedules = data.schedules;
                globalScheduleData.timeSetSuccessfully = data.timeSetSuccessfully;
                // No actualizamos currentTime ni currentDate aquí para evitar saltos si el cliente ya está mostrando su hora.
                // La hora del servidor solo se toma como referencia inicial o después de un setTime.
                updateFullUI(); // Actualiza la UI con los datos más recientes, excepto la hora si ya está corriendo localmente.
            })
            .catch(error => console.error('Error fetching status data:', error));
    }

    function updateFullUI() {
        // Actualizar estado del timbre
        timbreStatusEl.textContent = globalScheduleData.timbreEnabled ? 'Activado' : 'Desactivado';
        timbreStatusEl.classList.toggle('enabled', globalScheduleData.timbreEnabled);
        timbreStatusEl.classList.toggle('disabled', !globalScheduleData.timbreEnabled);
        
        // Actualizar select de configuración activa
        activeScheduleSelect.value = globalScheduleData.activeSchedule;
        
        // Actualizar nombres de configuraciones en inputs y en el select
        for (let i = 0; i < 3; i++) {
            if (scheduleNameInputs[i]) scheduleNameInputs[i].value = globalScheduleData.scheduleNames[i] || `Horario ${i+1}`;
            if (activeScheduleSelect.options[i]) activeScheduleSelect.options[i].textContent = globalScheduleData.scheduleNames[i] || `Horario ${i+1}`;
            if (scheduleTabNameSpans[i]) scheduleTabNameSpans[i].textContent = globalScheduleData.scheduleNames[i] || `Horario ${i+1}`;
        }
        
        // Actualizar horarios (campanas) para cada configuración
        for (let i = 0; i < 3; i++) {
            renderBells(i, globalScheduleData.schedules[i] || []);
        }

        // Actualizar estado de sincronización de hora
        updateDeviceTimeStatus();
    }

    function updateDeviceTimeStatus() {
        if (globalScheduleData.timeSetSuccessfully) {
            timeSyncStatusEl.textContent = "(Sincronizada)";
            timeSyncStatusEl.className = 'time-status synced';
        } else {
            timeSyncStatusEl.textContent = "(HORA NO AJUSTADA)";
            timeSyncStatusEl.className = 'time-status not-synced';
        }
    }
    
    function displayClientTime() {
        // Esta función ahora muestra la hora del cliente, no la del ESP32 directamente
        // La sincronización se hace al cargar y al usar "Ajustar Fecha y Hora"
        const now = new Date();
        const day = String(now.getDate()).padStart(2, '0');
        const month = String(now.getMonth() + 1).padStart(2, '0'); // Meses son 0-indexados
        const year = now.getFullYear();
        const hours = String(now.getHours()).padStart(2, '0');
        const minutes = String(now.getMinutes()).padStart(2, '0');
        const seconds = String(now.getSeconds()).padStart(2, '0');
        
        currentTimeDateEl.textContent = `${day}/${month}/${year} ${hours}:${minutes}:${seconds}`;
    }
    
    function renderBells(scheduleIndex, bellsArray) {
        const container = bellsContainers[scheduleIndex];
        if (!container) return; // Si el contenedor no existe
        container.innerHTML = ''; // Limpiar contenido anterior
        
        const bells = Array.isArray(bellsArray) ? bellsArray : []; // Asegurar que sea un array

        if (bells.length === 0) {
            container.innerHTML = '<p class="no-bells">No hay timbres configurados para este horario.</p>';
            return;
        }
        
        bells.forEach((bell, index) => {
            const bellElement = document.createElement('div');
            bellElement.className = 'bell-item';
            
            // Asegurar que hour y minute sean números antes de padStart
            const hourVal = typeof bell.hour === 'number' ? bell.hour : 0;
            const minuteVal = typeof bell.minute === 'number' ? bell.minute : 0;

            bellElement.innerHTML = `
                <input type="number" class="bell-hour" min="0" max="23" value="${hourVal}" title="Hora (0-23)">
                <span>:</span>
                <input type="number" class="bell-minute" min="0" max="59" value="${minuteVal}" title="Minuto (0-59)">
                <label class="end-of-day-label">
                    <input type="checkbox" class="bell-isEndOfDay" ${bell.isEndOfDay ? 'checked' : ''} title="Marcar si es el último timbre del día"> Fin de Jornada
                </label>
                <button class="delete-bell btn-small btn-danger" data-index="${index}" title="Eliminar este timbre">🗑️</button>
            `;
            
            container.appendChild(bellElement);
            
            bellElement.querySelector('.delete-bell').addEventListener('click', function() {
                deleteBellConfirm(scheduleIndex, index);
            });
        });
    }
    
    function addNewBellUI(scheduleIndex) {
        // Asegurar que la estructura de datos existe
        if (!globalScheduleData.schedules) globalScheduleData.schedules = [[], [], []];
        if (!Array.isArray(globalScheduleData.schedules[scheduleIndex])) {
            globalScheduleData.schedules[scheduleIndex] = [];
        }
        
        globalScheduleData.schedules[scheduleIndex].push({
            hour: 7, // Hora por defecto para nuevo timbre
            minute: 0,
            isEndOfDay: false
        });
        
        renderBells(scheduleIndex, globalScheduleData.schedules[scheduleIndex]);
    }

    function deleteBellConfirm(scheduleIndex, bellIdxToDelete) {
        if (confirm("¿Estás seguro de que quieres eliminar este timbre?")) {
            if (globalScheduleData.schedules && globalScheduleData.schedules[scheduleIndex]) {
                globalScheduleData.schedules[scheduleIndex].splice(bellIdxToDelete, 1);
                renderBells(scheduleIndex, globalScheduleData.schedules[scheduleIndex]);
                // Considera si quieres guardar automáticamente aquí o esperar al botón "Guardar Horario"
                // saveSchedule(scheduleIndex); // Descomentar para guardado automático tras borrar
            }
        }
    }
    
    function saveSchedule(scheduleIndex) {
        const scheduleName = scheduleNameInputs[scheduleIndex] ? scheduleNameInputs[scheduleIndex].value.trim() : `Horario ${scheduleIndex + 1}`;
        const container = bellsContainers[scheduleIndex];
        if (!container) {
            alert(`Error: No se encontró el contenedor para el horario ${scheduleIndex + 1}`);
            return;
        }

        const bellItems = container.querySelectorAll('.bell-item');
        const updatedBellsData = [];
        
        let formValid = true;
        bellItems.forEach(item => {
            const hour = parseInt(item.querySelector('.bell-hour').value);
            const minute = parseInt(item.querySelector('.bell-minute').value);
            const isEndOfDay = item.querySelector('.bell-isEndOfDay').checked;

            if (isNaN(hour) || hour < 0 || hour > 23 || isNaN(minute) || minute < 0 || minute > 59) {
                formValid = false;
            }
            updatedBellsData.push({ hour, minute, isEndOfDay });
        });

        if (!formValid) {
            alert("Por favor, verifica que todas las horas y minutos sean válidos (Hora: 0-23, Minuto: 0-59).");
            return;
        }
        
        // Ordenar timbres por hora y minuto antes de guardar
        updatedBellsData.sort((a, b) => {
            if (a.hour === b.hour) {
                return a.minute - b.minute;
            }
            return a.hour - b.hour;
        });

        const dataToSend = {
            scheduleIndex: scheduleIndex,
            name: scheduleName,
            bells: updatedBellsData
        };
        
        showSpinner(); // Mostrar indicador de carga
        fetch('/api/updateSchedule', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: 'data=' + encodeURIComponent(JSON.stringify(dataToSend))
        })
        .then(response => {
            if (!response.ok) {
                return response.text().then(text => { throw new Error(text || `Error del servidor: ${response.status}`) });
            }
            return response.text();
        })
        .then(result => {
            alert(`Horario '${scheduleName}' guardado correctamente.`);
            // Actualizar datos globales con lo que se acaba de guardar
            globalScheduleData.scheduleNames[scheduleIndex] = scheduleName;
            globalScheduleData.schedules[scheduleIndex] = updatedBellsData;
            updateFullUI(); // Refrescar toda la UI, incluyendo los nombres en el select
        })
        .catch(error => {
            console.error('Error saving schedule:', error);
            alert(`Error al guardar el horario: ${error.message}`);
        })
        .finally(hideSpinner);
    }
    
    function toggleTimbre() {
        showSpinner();
        fetch('/api/toggleTimbre', { method: 'POST' })
            .then(response => response.text())
            .then(result => {
                // fetchData(); // El fetchStatusData periódico lo hará
                // Actualizar directamente el estado local para respuesta más rápida
                globalScheduleData.timbreEnabled = !globalScheduleData.timbreEnabled;
                updateFullUI();
            })
            .catch(error => console.error('Error toggling timbre:', error))
            .finally(hideSpinner);
    }
    
    function ringNow() {
        if (!globalScheduleData.timeSetSuccessfully) {
            alert("No se puede activar el timbre porque la hora del sistema no está configurada. Por favor, ajuste la fecha y hora.");
            return;
        }
        showSpinner();
        fetch('/api/ringNow', { method: 'POST' })
            .then(response => response.text())
            .then(result => alert(result)) // Mostrar mensaje del servidor
            .catch(error => {
                console.error('Error ringing bell:', error);
                alert('Error al activar el timbre manualmente.');
            })
            .finally(hideSpinner);
    }
    
    function setActiveSchedule() {
        const scheduleIndex = activeScheduleSelect.value;
        showSpinner();
        fetch('/api/setActive', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: 'schedule=' + scheduleIndex
        })
        .then(response => response.text())
        .then(result => {
            // fetchData(); // El fetchStatusData periódico lo hará
            globalScheduleData.activeSchedule = parseInt(scheduleIndex);
            updateFullUI(); // Actualizar UI inmediatamente
        })
        .catch(error => console.error('Error setting active schedule:', error))
        .finally(hideSpinner);
    }
    
    function setSystemDateTime() {
        const day = parseInt(dayInput.value);
        const month = parseInt(monthInput.value);
        const year = parseInt(yearInput.value);
        const hour = parseInt(hourInput.value);
        const minute = parseInt(minuteInput.value);

        // Validaciones
        if (isNaN(day) || day < 1 || day > 31 ||
            isNaN(month) || month < 1 || month > 12 ||
            isNaN(year) || year < 2024 || year > 2099 || // Ajusta el rango de años si es necesario
            isNaN(hour) || hour < 0 || hour > 23 ||
            isNaN(minute) || minute < 0 || minute > 59) {
            
            setTimeMessageEl.textContent = 'Por favor, ingresa una fecha y hora válidas.';
            setTimeMessageEl.className = 'message error';
            return;
        }
        
        // Validar días del mes (simplificado, no considera bisiestos exactos para febrero)
        const daysInMonth = [0, 31, (year % 4 === 0 && year % 100 !== 0) || year % 400 === 0 ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
        if (day > daysInMonth[month]) {
            setTimeMessageEl.textContent = `Fecha inválida: El mes ${month} del año ${year} solo tiene ${daysInMonth[month]} días.`;
            setTimeMessageEl.className = 'message error';
            return;
        }
        
        setTimeMessageEl.textContent = ''; // Limpiar mensaje previo
        setTimeMessageEl.className = 'message';
        showSpinner();

        const params = new URLSearchParams();
        params.append('year', year);
        params.append('month', month);
        params.append('day', day);
        params.append('hour', hour);
        params.append('minute', minute);

        fetch('/api/setTime', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: params.toString()
        })
        .then(response => {
            if (!response.ok) {
                 return response.text().then(text => { throw new Error(text || `Error del servidor: ${response.status}`) });
            }
            return response.text();
        })
        .then(result => {
            setTimeMessageEl.textContent = result; // "Fecha y Hora actualizada: DD/MM/YYYY HH:MM:SS"
            setTimeMessageEl.className = 'message success';
            
            // Actualizar estado de sincronización y hora localmente
            globalScheduleData.timeSetSuccessfully = true;
            globalScheduleData.currentDate = `${String(day).padStart(2,'0')}-${String(month).padStart(2,'0')}-${year}`; // Asumir formato del servidor
            globalScheduleData.currentTime = `${String(hour).padStart(2,'0')}:${String(minute).padStart(2,'0')}:00`; // Asumir segundos 00
            updateDeviceTimeStatus(); // Actualiza el (Sincronizada)
            
            // Forzar la actualización de la hora mostrada al cliente para reflejar el cambio inmediatamente
            // Esto es opcional, ya que displayClientTime se ejecuta cada segundo.
            // Pero para feedback inmediato:
            const now = new Date(year, month - 1, day, hour, minute, 0); // Mes -1 para el constructor de Date
            const clientDay = String(now.getDate()).padStart(2, '0');
            const clientMonth = String(now.getMonth() + 1).padStart(2, '0');
            const clientYear = now.getFullYear();
            const clientHours = String(now.getHours()).padStart(2, '0');
            const clientMinutes = String(now.getMinutes()).padStart(2, '0');
            const clientSeconds = String(now.getSeconds()).padStart(2, '0');
            currentTimeDateEl.textContent = `${clientDay}/${clientMonth}/${clientYear} ${clientHours}:${clientMinutes}:${clientSeconds}`;


        })
        .catch(error => {
            console.error('Error setting system time:', error);
            setTimeMessageEl.textContent = `Error al ajustar la hora: ${error.message}`;
            setTimeMessageEl.className = 'message error';
        })
        .finally(hideSpinner);
    }

    // Funciones auxiliares para feedback visual (spinner)
    function showSpinner() {
        // Puedes implementar un spinner visual aquí si lo deseas
        // Por ejemplo, mostrar un elemento overlay con un gif animado
        document.body.style.cursor = 'wait'; // Cambia el cursor a espera
    }

    function hideSpinner() {
        document.body.style.cursor = 'default'; // Restaura el cursor
    }

});
