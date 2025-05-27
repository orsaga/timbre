// script.js
document.addEventListener('DOMContentLoaded', function() {
    // Variables globales
    let scheduleData = {
        timbreEnabled: false,
        activeSchedule: 0,
        scheduleNames: ["Horario 1", "Horario 2", "Horario 3"],
        schedules: [[], [], []]
    };
    
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

    if (!currentTimeEl || !currentDateEl || !timbreStatusEl || !activeScheduleSelect) {
        console.error("Error crítico: No se encontraron algunos elementos esenciales del DOM. Verifica los IDs en tu HTML.");
        return;
    }
    
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
    
    fetchData(); 
    displayCurrentDate(); 
    updateClientTime();   
    setInterval(updateClientTime, 1000);
    setInterval(fetchData, 60000);
    
    if (toggleTimbreBtn) toggleTimbreBtn.addEventListener('click', toggleTimbre);
    if (ringNowBtn) ringNowBtn.addEventListener('click', ringNow);
    if (activeScheduleSelect) activeScheduleSelect.addEventListener('change', setActiveSchedule);
    if (setTimeBtn) setTimeBtn.addEventListener('click', setSystemTime);
    
    addBellBtns.forEach(btn => {
        if (btn) {
            btn.addEventListener('click', function() {
                addNewBell(parseInt(this.getAttribute('data-schedule')));
            });
        }
    });
    
    saveBtns.forEach(btn => {
        if (btn) {
            btn.addEventListener('click', function() {
                saveSchedule(parseInt(this.getAttribute('data-schedule')));
            });
        }
    });
    
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
                if (data && typeof data === 'object' && data.schedules && data.scheduleNames) {
                    scheduleData = data;
                    updateUI(scheduleData);
                    console.log("fetchData completado, UI actualizada.");
                } else {
                    console.error('Datos recibidos de /api/status incompletos:', data);
                    updateUI(scheduleData); 
                }
            })
            .catch(error => {
                console.error('Error en fetchData:', error);
                updateUI(scheduleData); 
                if (currentTimeEl && (currentTimeEl.textContent.includes("Cargando") || !currentTimeEl.textContent.includes(':'))) {
                    currentTimeEl.textContent = "Error Conexión";
                    currentTimeEl.style.color = "red";
                }
            });
    }
    
    function updateUI(data) {
        console.log("Actualizando UI con datos:", data);
        if (timbreStatusEl) {
            timbreStatusEl.textContent = data.timbreEnabled ? 'Activado' : 'Desactivado';
            timbreStatusEl.style.color = data.timbreEnabled ? 'var(--success-color)' : 'var(--accent-color)';
        }
        if (activeScheduleSelect) {
            activeScheduleSelect.value = String(data.activeSchedule); 
        }
        for (let i = 0; i < 3; i++) {
            if (scheduleNameInputs[i] && data.scheduleNames && data.scheduleNames[i] !== undefined) {
                scheduleNameInputs[i].value = data.scheduleNames[i];
            }
            if (activeScheduleSelect && activeScheduleSelect.options[i] && data.scheduleNames && data.scheduleNames[i] !== undefined) {
                activeScheduleSelect.options[i].textContent = data.scheduleNames[i];
            }
        }
        for (let i = 0; i < 3; i++) {
            if (bellsContainers[i]) {
                renderBells(i, (data.schedules && data.schedules[i]) ? data.schedules[i] : []);
            }
        }
    }
    
    function updateClientTime() {
        const now = new Date();
        const hours = String(now.getHours()).padStart(2, '0');
        const minutes = String(now.getMinutes()).padStart(2, '0');
        const seconds = String(now.getSeconds()).padStart(2, '0');
        if (currentTimeEl) { 
            currentTimeEl.textContent = `${hours}:${minutes}:${seconds}`;
            if (currentTimeEl.style.color === "red") { 
                currentTimeEl.style.color = ""; 
            }
        }
    }

    function displayCurrentDate() {
        console.log('Intentando mostrar la fecha del cliente...');
        if (!currentDateEl) {
            console.error('Error: No se encontró el elemento con id="currentDate".');
            return;
        }
        const now = new Date();
        const options = { weekday: 'long', year: 'numeric', month: 'long', day: 'numeric' };
        let formattedDate;
        try {
            formattedDate = now.toLocaleDateString('es-ES', options);
            if (formattedDate && formattedDate.length > 0) {
                formattedDate = formattedDate.charAt(0).toUpperCase() + formattedDate.slice(1);
            } else {
                formattedDate = "Fecha no disponible";
            }
        } catch (e) {
            formattedDate = "Error al cargar fecha";
        }
        currentDateEl.textContent = formattedDate;
        console.log('Fecha del cliente mostrada:', formattedDate);
    }
    
     const HTML_ESCAPE_MAP = {
        '&': '&',
        '<': '<',
        '>': '>',
        '"': '"',
        "'": "'" // o puedes usar "'"
    };

    function escapeHTML(str) {
        if (typeof str !== 'string') return '';
        // La expresión regular [Á<>"'] coincide con cualquiera de los caracteres que son claves en HTML_ESCAPE_MAP
        return str.replace(/[&<>"']/g, (match) => HTML_ESCAPE_MAP[match]);
    }

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
            const bellName = bell.name || '';
            const bellType = bell.type || 'normal'; 

            bellElement.innerHTML = `
                <div class="bell-name"><input type="text" class="bell-name-input" placeholder="Nombre de la clase" value="${escapeHTML(bellName)}"></div>
                <div class="bell-time"><input type="number" class="bell-hour" min="0" max="23" value="${bell.hour}" placeholder="Hora"></div>
                <div class="bell-time"><input type="number" class="bell-minute" min="0" max="59" value="${bell.minute}" placeholder="Min"></div>
                <div class="bell-options">
                    <select class="bell-type">
                        <option value="normal" ${bellType === 'normal' ? 'selected' : ''}>Normal</option>
                        <option value="primer_descanso" ${bellType === 'primer_descanso' ? 'selected' : ''}>Primer Descanso</option>
                        <option value="segundo_descanso" ${bellType === 'segundo_descanso' ? 'selected' : ''}>Segundo Descanso</option>
                        <option value="fin_jornada" ${bellType === 'fin_jornada' ? 'selected' : ''}>Fin de Jornada</option>
                    </select>
                </div>
                <div><span>${hourFormatted}:${minuteFormatted}</span></div>
                <button class="delete-bell" data-index="${index}">❌</button>
            `; // Asegúrate que este backtick (`) de cierre esté presente y sea el correcto.
            
            container.appendChild(bellElement);
            
            // No es necesario establecer select.value si usamos 'selected' en las options
            // const selectElement = bellElement.querySelector('.bell-type');
            // if (selectElement) selectElement.value = bellType; 
            
            const deleteButton = bellElement.querySelector('.delete-bell');
            if (deleteButton) {
                deleteButton.addEventListener('click', function() {
                    deleteBell(scheduleIndex, parseInt(this.getAttribute('data-index')));
                });
            }
        });
    }
    
    function addNewBell(scheduleIndex) {
        if (!scheduleData.schedules) scheduleData.schedules = [[], [], []];
        if (!scheduleData.schedules[scheduleIndex]) scheduleData.schedules[scheduleIndex] = [];
        scheduleData.schedules[scheduleIndex].push({ name: "", hour: 8, minute: 0, type: 'normal' });
        if (bellsContainers[scheduleIndex]) {
            renderBells(scheduleIndex, scheduleData.schedules[scheduleIndex]);
        }
    }
    
    function deleteBell(scheduleIndex, bellIndex) {
        if (scheduleData.schedules && scheduleData.schedules[scheduleIndex]) {
            scheduleData.schedules[scheduleIndex].splice(bellIndex, 1);
            if (bellsContainers[scheduleIndex]) {
                renderBells(scheduleIndex, scheduleData.schedules[scheduleIndex]);
            }
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
            if (hasInvalidTime) return;
            const nameInput = item.querySelector('.bell-name-input');
            const hourInputEl = item.querySelector('.bell-hour');
            const minuteInputEl = item.querySelector('.bell-minute');
            const typeSelect = item.querySelector('.bell-type');

            if (!nameInput || !hourInputEl || !minuteInputEl || !typeSelect) return;

            const name = nameInput.value;
            const hour = parseInt(hourInputEl.value);
            const minute = parseInt(minuteInputEl.value);
            const type = typeSelect.value;
            
            if (isNaN(hour) || hour < 0 || hour > 23 || isNaN(minute) || minute < 0 || minute > 59) {
                alert(`Hora inválida para "${name || 'sin nombre'}". No se guardará.`);
                hasInvalidTime = true;
            } else {
                updatedBells.push({ name, hour, minute, type });
            }
        });

        if (hasInvalidTime) return;
        
        const dataToSave = { scheduleIndex, name: scheduleName, bells: updatedBells };
        console.log("Guardando horario:", dataToSave);
        fetch('/api/updateSchedule', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: 'data=' + encodeURIComponent(JSON.stringify(dataToSave))
        })
        .then(response => {
            if (!response.ok) return response.text().then(text => { throw new Error(`Servidor: ${response.status} ${text}`); });
            return response.text();
        })
        .then(result => { alert('Configuración guardada.'); fetchData(); })
        .catch(error => { alert(`Error al guardar: ${error.message}`); });
    }
    
    function toggleTimbre() {
        fetch('/api/toggleTimbre', { method: 'POST' })
            .then(response => response.text()).then(() => fetchData())
            .catch(error => alert("Error al cambiar estado del timbre."));
    }
    
    function ringNow() {
        fetch('/api/ringNow', { method: 'POST' })
            .then(response => response.text()).then(() => alert('Timbre activado manualmente.'))
            .catch(error => alert("Error al activar timbre manualmente."));
    }
    
    function setActiveSchedule() {
        if (!activeScheduleSelect) return;
        fetch('/api/setActive', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: 'schedule=' + activeScheduleSelect.value
        })
        .then(response => response.text()).then(() => { alert('Config. activa cambiada.'); fetchData(); })
        .catch(error => alert("Error al cambiar config. activa."));
    }
    
    function setSystemTime() {
        if (!hourInput || !minuteInput) return;
        const hour = parseInt(hourInput.value);
        const minute = parseInt(minuteInput.value);
        if (isNaN(hour) || hour < 0 || hour > 23 || isNaN(minute) || minute < 0 || minute > 59) {
            alert('Hora inválida.'); return;
        }
        fetch('/api/setTime', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: `hour=${hour}&minute=${minute}`
        })
        .then(response => response.text()).then(() => alert('Hora del sistema actualizada.'))
        .catch(error => alert("Error al ajustar la hora."));
    }

});

console.log('script.js cargado y parseado completamente.');