// script.js
document.addEventListener('DOMContentLoaded', function() {
    // Definición de los estados del timbre para el frontend (debe coincidir con el enum en el ESP32)
    const BELL_STATE_ENUM = {
        ACTIVE: 0,
        PAUSED_WEEKEND: 1,
        PAUSED_HOLIDAY: 2,
        DEACTIVATED_MANUAL: 3
    };

    // Para la sanitización de cadenas HTML
    const HTML_ESCAPE_MAP = {
        '&': '&amp;',
        '<': '&lt;',
        '>': '&gt;',
        '"': '&quot;',
        "'": '&#39;'
    };

    function escapeHTML(str) {
        // Asegurarse de que el input sea una cadena para evitar errores de .replace en no-strings
        if (typeof str !== 'string') return '';
        return str.replace(/[&<>"']/g, (match) => HTML_ESCAPE_MAP[match]);
    }

    // Variables globales
    let scheduleData = null; // Se inicializará con los datos del servidor

    // Referencias a elementos del DOM
    const currentTimeEl = document.getElementById('currentTime');
    const currentDateEl = document.getElementById('currentDate');
    const timbreStatusEl = document.getElementById('timbreStatus');
    const toggleTimbreBtn = document.getElementById('toggleTimbre');
    const ringNowBtn = document.getElementById('ringNow');
    const activeScheduleSelect = document.getElementById('activeSchedule');
    const setTimeBtn = document.getElementById('setTimeBtn');
    const hourInput = document.getElementById('hourInput');
    const minuteInput = document.getElementById('minuteInput');
    const dayInput = document.getElementById('dayInput');
    const monthInput = document.getElementById('monthInput');
    const yearInput = document.getElementById('yearInput');
    const tabButtons = document.querySelectorAll('.tab-btn');
    const tabContents = document.querySelectorAll('.tab-content');
    
    const scheduleNameInputs = [
        document.getElementById('schedule1Name'),
        document.getElementById('schedule2Name'),
        document.getElementById('schedule3Name')
    ];
    const bellsContainers = [
        document.getElementById('bells1'),
        document.getElementById('bells2'),
        document.getElementById('bells3')
    ];
    const addBellBtns = document.querySelectorAll('.add-bell-btn');
    const saveBtns = document.querySelectorAll('.save-btn');

    // Validación inicial de elementos DOM
    if (!currentTimeEl || !currentDateEl || !timbreStatusEl || !activeScheduleSelect || !toggleTimbreBtn || !ringNowBtn || !setTimeBtn || !hourInput || !minuteInput || !dayInput || !monthInput || !yearInput) {
        console.error("Error crítico: No se encontraron algunos elementos esenciales del DOM. Verifica los IDs en tu HTML.");
        // alert("Error al cargar la interfaz. Algunos elementos no se encontraron. Consulta la consola.");
        return;
    }
    
    // Asignación de event listeners a los botones de tabulación
    tabButtons.forEach(button => {
        button.addEventListener('click', function() {
            const tabId = this.getAttribute('data-tab');
            tabButtons.forEach(btn => btn.classList.remove('active'));
            tabContents.forEach(content => content.classList.remove('active'));
            this.classList.add('active');
            const targetTabContent = document.getElementById(tabId);
            if (targetTabContent) {
                targetTabContent.classList.add('active');
            } else {
                console.error(`Contenido de tab no encontrado para ID: ${tabId}`);
            }
        });
    });
    
    // Carga inicial de datos y actualización contínua
    fetchData(); 
    updateClientTime(); 
    setInterval(updateClientTime, 1000); // Actualiza la hora del cliente cada segundo
    setInterval(fetchData, 30000); // Actualiza datos de la ESP32 cada 30 segundos
    
    // Event listeners para botones de control
    toggleTimbreBtn.addEventListener('click', toggleTimbre);
    ringNowBtn.addEventListener('click', ringNow);
    activeScheduleSelect.addEventListener('change', setActiveSchedule);
    setTimeBtn.addEventListener('click', setSystemTime);
    
    // Event listeners para botones de añadir y guardar horario
    addBellBtns.forEach(btn => {
        btn.addEventListener('click', function() {
            addNewBell(parseInt(this.getAttribute('data-schedule')));
        });
    });
    
    saveBtns.forEach(btn => {
        btn.addEventListener('click', function() {
            saveSchedule(parseInt(this.getAttribute('data-schedule')));
        });
    });
    
    // --- Funciones de Comunicación y Actualización de UI ---
    
    function fetchData() {
        console.log("Iniciando fetchData...");
        fetch('/api/status')
            .then(response => {
                console.log("Respuesta recibida de /api/status, estado:", response.status);
                if (!response.ok) {
                    return response.text().then(text => { 
                        throw new Error(`Error HTTP! estado: ${response.status}, texto: ${text || response.statusText}`);
                    });
                }
                return response.json();
            })
            .then(data => {
                console.log("Datos JSON parseados:", data);
                // Validar la estructura mínima esperada
                if (data && typeof data === 'object' && Array.isArray(data.schedules) && Array.isArray(data.scheduleNames) && data.bellOperationalState !== undefined) {
                    scheduleData = data; // Guarda los datos recibidos
                    updateUI(scheduleData);
                    console.log("fetchData completado, UI actualizada.");
                } else {
                    console.error('Datos recibidos de /api/status incompletos o inválidos:', data);
                    // Si los datos son incompletos, pero hay algo, al menos intenta actualizar lo que se pueda.
                    // Si scheduleData es null (primera carga y fallo), la UI quedará con los valores iniciales.
                    if (scheduleData) updateUI(scheduleData);
                }
            })
            .catch(error => {
                console.error('Error en fetchData:', error);
                // Muestra un mensaje de error en la UI si hay problemas de conexión
                if (currentTimeEl) {
                    currentTimeEl.textContent = "Error Conexión";
                    currentTimeEl.style.color = "red";
                }
                if (timbreStatusEl) {
                    timbreStatusEl.textContent = "Error Conexión";
                    timbreStatusEl.className = 'status-value disconnected-red'; // Cambiar clase para indicar desconexión
                }
                // Si no hay datos, deshabilitar botones de control
                toggleTimbreBtn.disabled = true;
                ringNowBtn.disabled = true;
                activeScheduleSelect.disabled = true;
                setTimeBtn.disabled = true;
                saveBtns.forEach(btn => btn.disabled = true);
                addBellBtns.forEach(btn => btn.disabled = true);
            });
    }
    
    function updateUI(data) {
        console.log("Actualizando UI con datos:", data);

        // Habilitar botones si la conexión es exitosa
        toggleTimbreBtn.disabled = false;
        ringNowBtn.disabled = false;
        activeScheduleSelect.disabled = false;
        setTimeBtn.disabled = false;
        saveBtns.forEach(btn => btn.disabled = false);
        addBellBtns.forEach(btn => btn.disabled = false);

        // Actualizar estado del timbre (texto y color)
        if (timbreStatusEl && data.bellOperationalState !== undefined) {
            let statusText = "";
            let statusClass = "status-value "; // Espacio final es intencional
            switch (data.bellOperationalState) {
                case BELL_STATE_ENUM.ACTIVE:
                    statusText = 'Activo';
                    statusClass += 'active-green';
                    break;
                case BELL_STATE_ENUM.PAUSED_WEEKEND:
                    statusText = 'En Pausa (Fin de Semana)';
                    statusClass += 'paused-orange';
                    break;
                case BELL_STATE_ENUM.PAUSED_HOLIDAY:
                    statusText = 'En Pausa (Día Festivo)';
                    statusClass += 'paused-yellow';
                    break;
                case BELL_STATE_ENUM.DEACTIVATED_MANUAL:
                    statusText = 'Desactivado';
                    statusClass += 'deactivated-red';
                    break;
                default:
                    statusText = 'Desconocido';
                    statusClass += 'disconnected-red';
            }
            timbreStatusEl.textContent = statusText;
            timbreStatusEl.className = statusClass;
        }

        // Actualizar texto del botón Activar/Desactivar
        if (toggleTimbreBtn) {
            toggleTimbreBtn.textContent = data.timbreEnabled ? 'Desactivar' : 'Activar';
        }

        // Reestablecer color de tiempo si estaba en rojo
        if (currentTimeEl && currentTimeEl.style.color === "red") { 
            currentTimeEl.style.color = ""; 
        }

        // Actualizar fecha del sistema
        if (currentDateEl && data.currentDate) {
            currentDateEl.textContent = data.currentDate;
        }

        // Actualizar selector de horario activo
        if (activeScheduleSelect && data.activeSchedule !== undefined) {
            activeScheduleSelect.value = String(data.activeSchedule); 
        }

        // Actualizar nombres de horarios y opciones del selector
        for (let i = 0; i < 3; i++) {
            if (scheduleNameInputs[i] && data.scheduleNames && data.scheduleNames[i] !== undefined) {
                scheduleNameInputs[i].value = data.scheduleNames[i];
            }
            if (activeScheduleSelect && activeScheduleSelect.options[i] && data.scheduleNames && data.scheduleNames[i] !== undefined) {
                activeScheduleSelect.options[i].textContent = data.scheduleNames[i];
            }
        }

        // Renderizar campanas para cada horario
        for (let i = 0; i < 3; i++) {
            if (bellsContainers[i]) {
                // Asegurarse de que data.schedules[i] exista y sea un array
                renderBells(i, (data.schedules && Array.isArray(data.schedules[i])) ? data.schedules[i] : []);
            }
        }
    }
    
    // Actualiza la hora local del usuario (no la del ESP32)
    function updateClientTime() {
        const now = new Date();
        const hours = String(now.getHours()).padStart(2, '0');
        const minutes = String(now.getMinutes()).padStart(2, '0');
        const seconds = String(now.getSeconds()).padStart(2, '0');
        if (currentTimeEl) { 
            currentTimeEl.textContent = `${hours}:${minutes}:${seconds}`;
        }
    }

    // --- Funciones de Gestión de Horarios y Campanas ---

    function renderBells(scheduleIndex, bells) {
        const container = bellsContainers[scheduleIndex];
        if (!container) return;
        container.innerHTML = ''; 
        if (!bells || bells.length === 0) {
            container.innerHTML = '<p class="no-bells">No hay timbres configurados.</p>';
            return;
        }
        bells.forEach((bell, index) => {
            const bellElement = document.createElement('div');
            bellElement.className = 'bell-item';
            
            const hourFormatted = String(bell.hour).padStart(2, '0');
            const minuteFormatted = String(bell.minute).padStart(2, '0');
            const bellName = bell.name !== undefined ? bell.name : ''; // Asegurarse de que `name` exista

            bellElement.innerHTML = `
                <div class="bell-name">
                    <input type="text" class="bell-name-input" placeholder="Nombre de la clase" value="${escapeHTML(bellName)}">
                </div>
                <div class="bell-time">
                    <input type="number" class="bell-hour" min="0" max="23" value="${bell.hour}" placeholder="Hora">
                </div>
                <div class="bell-time">
                    <input type="number" class="bell-minute" min="0" max="59" value="${bell.minute}" placeholder="Min">
                </div>
                <div class="bell-options">
                    <select class="bell-type">
                        <option value="false" ${!bell.isEndOfDay ? 'selected' : ''}>Normal</option>
                        <option value="true" ${bell.isEndOfDay ? 'selected' : ''}>Fin de Jornada</option>
                    </select>
                </div>
                <div class="bell-display-time"><span>${hourFormatted}:${minuteFormatted}</span></div>
                <button class="delete-bell" data-index="${index}">❌</button>
            `;
            
            container.appendChild(bellElement);
            
            // Asignar evento al botón de eliminar
            const deleteButton = bellElement.querySelector('.delete-bell');
            if (deleteButton) {
                // Usamos una función anónima para capturar el 'index' actual
                deleteButton.addEventListener('click', () => deleteBell(scheduleIndex, index));
            }
        });
    }
    
    function addNewBell(scheduleIndex) {
        // Asegúrate de que scheduleData y schedules[scheduleIndex] existan
        if (!scheduleData || !scheduleData.schedules) {
            // Inicializar con valores por defecto si no existen
            scheduleData = {
                schedules: [[], [], []],
                scheduleNames: ["Horario 1", "Horario 2", "Horario 3"],
                activeSchedule: 0,
                timbreEnabled: true,
                bellOperationalState: 0,
                currentDate: "",
                currentTime: ""
            };
        }
        if (!scheduleData.schedules[scheduleIndex]) {
            scheduleData.schedules[scheduleIndex] = [];
        }
        scheduleData.schedules[scheduleIndex].push({ name: "", hour: 8, minute: 0, isEndOfDay: false });
        renderBells(scheduleIndex, scheduleData.schedules[scheduleIndex]);
    }
    
    function deleteBell(scheduleIndex, bellIndex) {
        if (scheduleData && scheduleData.schedules && scheduleData.schedules[scheduleIndex]) {
            // Eliminar el elemento del arreglo de datos local
            scheduleData.schedules[scheduleIndex].splice(bellIndex, 1);
            // Volver a renderizar para que la UI refleje el cambio y los índices sean correctos
            renderBells(scheduleIndex, scheduleData.schedules[scheduleIndex]); 
        }
    }
    
    function saveSchedule(scheduleIndex) {
        const container = bellsContainers[scheduleIndex];
        if (!container) { alert("Error: Contenedor de horarios no encontrado."); return; }
        const currentScheduleNameInput = scheduleNameInputs[scheduleIndex];
        if (!currentScheduleNameInput) { alert("Error: Campo de nombre del horario no encontrado."); return; }

        const scheduleName = currentScheduleNameInput.value;
        const updatedBells = [];
        let hasInvalidTime = false; 
        
        container.querySelectorAll('.bell-item').forEach(item => {
            if (hasInvalidTime) return; // Si ya hay un error, no procesar más
            const nameInput = item.querySelector('.bell-name-input');
            const hourInputEl = item.querySelector('.bell-hour');
            const minuteInputEl = item.querySelector('.bell-minute');
            const typeSelect = item.querySelector('.bell-type'); // Select para isEndOfDay

            if (!nameInput || !hourInputEl || !minuteInputEl || !typeSelect) {
                console.warn("Elemento de campana incompleto, omitiendo.", item);
                return;
            }

            const name = nameInput.value.trim(); // Leer y limpiar el nombre
            const hour = parseInt(hourInputEl.value);
            const minute = parseInt(minuteInputEl.value);
            const isEndOfDay = typeSelect.value === 'true'; // El valor del select es 'true' o 'false' (string)

            if (isNaN(hour) || hour < 0 || hour > 23 || isNaN(minute) || minute < 0 || minute > 59) {
                alert(`Hora o minuto inválido para "${name || 'sin nombre'}". Por favor, corrige.`);
                hasInvalidTime = true;
            } else {
                updatedBells.push({ name, hour, minute, isEndOfDay });
            }
        });

        if (hasInvalidTime) return; // Detener si hubo algún error
        
        // Ordenar los timbres por hora y minuto antes de guardar
        updatedBells.sort((a,b) => (a.hour * 60 + a.minute) - (b.hour * 60 + b.minute)); 
        
        const dataToSave = { scheduleIndex, name: scheduleName, bells: updatedBells };
        console.log("Guardando horario:", dataToSave);

        fetch('/api/updateSchedule', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: 'data=' + encodeURIComponent(JSON.stringify(dataToSave)) // Codifica el JSON para la URL
        })
        .then(response => {
            if (!response.ok) return response.text().then(text => { throw new Error(`Servidor: ${response.status} - ${text}`); });
            return response.text();
        })
        .then(result => { alert('Configuración guardada.'); fetchData(); })
        .catch(error => { alert(`Error al guardar: ${error.message}`); console.error("Error guardando horario:", error); });
    }
    
    // --- Funciones de Control General ---

    function toggleTimbre() {
        fetch('/api/toggleTimbre', { method: 'POST' })
            .then(response => {
                if (!response.ok) return response.text().then(text => { throw new Error(`Servidor: ${response.status} - ${text}`); });
                return response.text();
            })
            .then(() => fetchData()) // Vuelve a cargar datos para actualizar la UI
            .catch(error => alert(`Error al cambiar estado del timbre: ${error.message}`));
    }
    
    function ringNow() {
        fetch('/api/ringNow', { method: 'POST' })
            .then(response => {
                if (!response.ok) return response.text().then(text => { throw new Error(`Servidor: ${response.status} - ${text}`); });
                return response.text();
            })
            .then(() => alert('Timbre activado manualmente.'))
            .catch(error => alert(`Error al activar timbre manualmente: ${error.message}`));
    }
    
    function setActiveSchedule() {
        if (!activeScheduleSelect) return;
        fetch('/api/setActive', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: 'schedule=' + activeScheduleSelect.value
        })
        .then(response => {
            if (!response.ok) return response.text().then(text => { throw new Error(`Servidor: ${response.status} - ${text}`); });
            return response.text();
        })
        .then(() => { alert('Configuración activa cambiada.'); fetchData(); })
        .catch(error => alert(`Error al cambiar config. activa: ${error.message}`));
    }
    
    function setSystemTime() {
        if (!hourInput || !minuteInput || !dayInput || !monthInput || !yearInput) return;
        const hour = parseInt(hourInput.value);
        const minute = parseInt(minuteInput.value);
        const day = parseInt(dayInput.value);
        const month = parseInt(monthInput.value);
        const year = parseInt(yearInput.value);

        // Validar el año y la fecha
        const dateCheck = new Date(year, month - 1, day, hour, minute); // Month is 0-indexed for Date object
        if (isNaN(dateCheck.getTime()) ||
            dateCheck.getFullYear() !== year ||
            dateCheck.getMonth() !== (month - 1) ||
            dateCheck.getDate() !== day ||
            dateCheck.getHours() !== hour ||
            dateCheck.getMinutes() !== minute || // Check if conversion back matches
            year < 2024 || year > 2050) // Rango de años de ejemplo, ajústalo si es necesario
        {
            alert('Fecha u hora inválida. Por favor, verifica todos los campos. Asegúrate que la fecha es válida.'); 
            return;
        }

        fetch('/api/setTime', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: `hour=${hour}&minute=${minute}&dayOfMonth=${day}&month=${month}&year=${year}`
        })
        .then(response => {
            if (!response.ok) return response.text().then(text => { throw new Error(`Servidor: ${response.status} - ${text}`); });
            return response.text();
        })
        .then(() => { alert('Hora y fecha del sistema actualizadas.'); fetchData(); })
        .catch(error => alert(`Error al ajustar la hora y fecha: ${error.message}`));
    }
    
});
console.log('script.js cargado y parseado completamente.');