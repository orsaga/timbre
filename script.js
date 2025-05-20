// script.js
document.addEventListener('DOMContentLoaded', function() {
    // Variables globales
    let scheduleData = [];
    
    // Referencias a elementos del DOM
    const currentTimeEl = document.getElementById('currentTime');
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
    
    // Configurar tabs
    tabButtons.forEach(button => {
        button.addEventListener('click', function() {
            const tabId = this.getAttribute('data-tab');
            
            // Remover clase active de todos los botones y contenidos
            tabButtons.forEach(btn => btn.classList.remove('active'));
            tabContents.forEach(content => content.classList.remove('active'));
            
            // Añadir clase active al botón y contenido actual
            this.classList.add('active');
            document.getElementById(tabId).classList.add('active');
        });
    });
    
    // Cargar datos iniciales
    fetchData();
    
    // Establecer temporizador para actualizar la hora
    setInterval(updateCurrentTime, 1000);
    
    // Establecer temporizador para refrescar los datos cada minuto
    setInterval(fetchData, 60000);
    
    // Configurar manejadores de eventos
    toggleTimbreBtn.addEventListener('click', toggleTimbre);
    ringNowBtn.addEventListener('click', ringNow);
    activeScheduleSelect.addEventListener('change', setActiveSchedule);
    setTimeBtn.addEventListener('click', setSystemTime);
    
    addBellBtns.forEach(btn => {
        btn.addEventListener('click', function() {
            const scheduleIndex = parseInt(this.getAttribute('data-schedule'));
            addNewBell(scheduleIndex);
        });
    });
    
    saveBtns.forEach(btn => {
        btn.addEventListener('click', function() {
            const scheduleIndex = parseInt(this.getAttribute('data-schedule'));
            saveSchedule(scheduleIndex);
        });
    });
    
    // Funciones
    function fetchData() {
        fetch('/api/status')
            .then(response => response.json())
            .then(data => {
                scheduleData = data;
                updateUI(data);
            })
            .catch(error => {
                console.error('Error fetching data:', error);
            });
    }
    
    function updateUI(data) {
        // Actualizar estado del timbre
        timbreStatusEl.textContent = data.timbreEnabled ? 'Activado' : 'Desactivado';
        timbreStatusEl.style.color = data.timbreEnabled ? 'var(--success-color)' : 'var(--accent-color)';
        
        // Actualizar configuración activa
        activeScheduleSelect.value = data.activeSchedule;
        
        // Actualizar nombres de configuraciones
        for (let i = 0; i < 3; i++) {
            scheduleNameInputs[i].value = data.scheduleNames[i];
            activeScheduleSelect.options[i].textContent = data.scheduleNames[i];
        }
        
        // Actualizar horarios para cada configuración
        for (let i = 0; i < 3; i++) {
            renderBells(i, data.schedules[i]);
        }
    }
    
    function updateCurrentTime() {
        // Obtener hora actual del sistema
        const now = new Date();
        const hours = String(now.getHours()).padStart(2, '0');
        const minutes = String(now.getMinutes()).padStart(2, '0');
        const seconds = String(now.getSeconds()).padStart(2, '0');
        
        // Actualizar elemento en la UI
        currentTimeEl.textContent = `${hours}:${minutes}:${seconds}`;
    }
    
    function renderBells(scheduleIndex, bells) {
        const container = bellsContainers[scheduleIndex];
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
            
            bellElement.innerHTML = `
                <div class="bell-name">
                    <input type="text" class="bell-name-input" placeholder="Nombre de la clase">
                </div>
                <div class="bell-time">
                    <input type="number" class="bell-hour" min="0" max="23" value="${bell.hour}" placeholder="Hora">
                </div>
                <div class="bell-time">
                    <input type="number" class="bell-minute" min="0" max="59" value="${bell.minute}" placeholder="Min">
                </div>
                <div class="bell-options">
                    <select class="bell-type">
                        <option value="normal">Normal</option>
                        <option value="primer_descanso">Primer Descanso</option>
                        <option value="segundo_descanso">Segundo Descanso</option>
                        <option value="fin_jornada">Fin de Jornada</option>
                    </select>
                </div>
                <div>
                    <span>${hourFormatted}:${minuteFormatted}</span>
                </div>
                <button class="delete-bell" data-index="${index}">❌</button>
            `;
            
            container.appendChild(bellElement);
            
            // Añadir evento para eliminar timbre
            bellElement.querySelector('.delete-bell').addEventListener('click', function() {
                const bellIndex = parseInt(this.getAttribute('data-index'));
                deleteBell(scheduleIndex, bellIndex);
            });
        });
    }
    
    function addNewBell(scheduleIndex) {
        if (!scheduleData.schedules) {
            scheduleData.schedules = [[], [], []];
        }
        
        if (!scheduleData.schedules[scheduleIndex]) {
            scheduleData.schedules[scheduleIndex] = [];
        }
        
        // Añadir nuevo timbre con valores predeterminados
        scheduleData.schedules[scheduleIndex].push({
            hour: 8,
            minute: 0,
            isEndOfDay: false
        });
        
        // Actualizar UI
        renderBells(scheduleIndex, scheduleData.schedules[scheduleIndex]);
    }
    
    function deleteBell(scheduleIndex, bellIndex) {
        if (scheduleData.schedules && scheduleData.schedules[scheduleIndex]) {
            // Eliminar timbre del array
            scheduleData.schedules[scheduleIndex].splice(bellIndex, 1);
            
            // Actualizar UI
            renderBells(scheduleIndex, scheduleData.schedules[scheduleIndex]);
        }
    }
    
    function saveSchedule(scheduleIndex) {
        // Recopilar datos actualizados de la UI
        
        const container = bellsContainers[scheduleIndex];
        const bellItems = container.querySelectorAll('.bell-item');
        const scheduleName = scheduleNameInputs[scheduleIndex].value;
        
        const updatedBells = [];
        
        bellItems.forEach(item => {
            const hour = parseInt(item.querySelector('.bell-hour').value);
            const minute = parseInt(item.querySelector('.bell-minute').value);
            const type = item.querySelector('.bell-type').value;
            updatedBells.push({ hour, minute, type });
            
        });
        
        // Preparar datos para enviar al servidor
        const data = {
            scheduleIndex,
            name: scheduleName,
            bells: updatedBells
        };
        
        // Enviar al servidor
        fetch('/api/updateSchedule', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
            },
            body: 'data=' + encodeURIComponent(JSON.stringify(data))
        })
        .then(response => response.text())
        .then(result => {
            alert('Configuración guardada correctamente');
            fetchData(); // Recargar datos actualizados
        })
        .catch(error => {
            console.error('Error saving schedule:', error);
            alert('Error al guardar la configuración');
        });
    }
    
    function toggleTimbre() {
        fetch('/api/toggleTimbre', {
            method: 'POST'
        })
        .then(response => response.text())
        .then(result => {
            fetchData(); // Recargar datos
        })
        .catch(error => {
            console.error('Error toggling timbre:', error);
        });
    }
    
    function ringNow() {
        fetch('/api/ringNow', {
            method: 'POST'
        })
        .then(response => response.text())
        .then(result => {
            alert('Timbre activado manualmente');
        })
        .catch(error => {
            console.error('Error ringing bell:', error);
        });
    }
    
    function setActiveSchedule() {
        const scheduleIndex = activeScheduleSelect.value;
        
        fetch('/api/setActive', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
            },
            body: 'schedule=' + scheduleIndex
        })
        .then(response => response.text())
        .then(result => {
            alert('Configuración activa cambiada');
            fetchData(); // Recargar datos
        })
        .catch(error => {
            console.error('Error setting active schedule:', error);
        });
    }
    
    function setSystemTime() {
        const hour = parseInt(hourInput.value);
        const minute = parseInt(minuteInput.value);
        
        if (isNaN(hour) || hour < 0 || hour > 23 || isNaN(minute) || minute < 0 || minute > 59) {
            alert('Por favor, ingresa una hora válida');
            return;
        }
        
        fetch('/api/setTime', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
            },
            body: `hour=${hour}&minute=${minute}`
        })
        .then(response => response.text())
        .then(result => {
            alert('Hora actualizada correctamente');
            updateCurrentTime(); // Actualizar hora mostrada
        })
        .catch(error => {
            console.error('Error setting time:', error);
        });
    }
});