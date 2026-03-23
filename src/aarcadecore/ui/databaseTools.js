/* databaseTools.js — Database merge tool */

function initDatabaseTools() {
    var content = arcadeHud.ui.createWindow({
        title: 'Database Tools',
        showBack: true,
        showClose: true,
        onBack: function() { window.history.back(); },
        onClose: function() { if (window.aapi && aapi.manager) aapi.manager.closeMenu(); }
    });

    /* ---- Merge Section ---- */
    var mergeTitle = document.createElement('h3');
    mergeTitle.style.cssText = 'color:#ccc; margin:0 0 8px 0; font-size:14px;';
    mergeTitle.textContent = 'Merge Library';
    content.appendChild(mergeTitle);

    var srcLabel = document.createElement('label');
    srcLabel.style.cssText = 'color:#aaa; display:block; margin-bottom:4px;';
    srcLabel.textContent = 'Library to merge:';
    content.appendChild(srcLabel);

    var srcInput = document.createElement('input');
    srcInput.type = 'text';
    srcInput.className = 'aa-edit-row-input';
    srcInput.style.cssText = 'width:100%; margin-bottom:12px;';
    srcInput.placeholder = 'e.g. G:/path/to/library.db';
    content.appendChild(srcInput);

    /* Strategy radio buttons */
    var stratLabel = document.createElement('label');
    stratLabel.style.cssText = 'color:#aaa; display:block; margin-bottom:4px;';
    stratLabel.textContent = 'Merge Strategy:';
    content.appendChild(stratLabel);

    var strategies = [
        { value: 'skip', label: 'Skip existing entries (safest — only add new entries)', checked: true },
        { value: 'overwrite', label: 'Overwrite all existing entries (replace with source data)' },
        { value: 'larger', label: 'Overwrite only if larger (slow)' }
    ];
    for (var s = 0; s < strategies.length; s++) {
        var radioLabel = document.createElement('label');
        radioLabel.style.cssText = 'color:#aaa; display:block; padding:4px 0; cursor:pointer;';
        var radio = document.createElement('input');
        radio.type = 'radio';
        radio.name = 'mergeStrategy';
        radio.value = strategies[s].value;
        if (strategies[s].checked) radio.checked = true;
        radio.style.cssText = 'margin-right:6px;';
        radioLabel.appendChild(radio);
        radioLabel.appendChild(document.createTextNode(strategies[s].label));
        content.appendChild(radioLabel);
    }

    var spacer = document.createElement('div');
    spacer.style.cssText = 'height:12px;';
    content.appendChild(spacer);

    var mergeBtn = document.createElement('button');
    mergeBtn.className = 'aa-btn';
    mergeBtn.textContent = 'Merge into Active Library';
    mergeBtn.addEventListener('click', function() {
        var sourcePath = srcInput.value.trim();
        if (!sourcePath) {
            mergeStatus.textContent = 'Enter a library path.';
            return;
        }
        var strategyEl = document.querySelector('input[name="mergeStrategy"]:checked');
        var strategy = strategyEl ? strategyEl.value : 'skip';
        mergeStatus.textContent = 'Merging...';
        mergeBtn.disabled = true;
        try {
            if (window.aapi && aapi.manager && aapi.manager.mergeLibrary) {
                var result = aapi.manager.mergeLibrary(sourcePath, strategy);
                if (result && result.success) {
                    mergeStatus.textContent = 'Merge complete. ' + result.stats;
                } else {
                    mergeStatus.textContent = 'Error: ' + (result ? result.error : 'Bridge not available');
                }
            } else {
                mergeStatus.textContent = 'Error: Bridge not available.';
            }
        } catch (e) {
            mergeStatus.textContent = 'Error: ' + e;
        }
        mergeBtn.disabled = false;
    });
    content.appendChild(mergeBtn);

    var mergeStatus = document.createElement('p');
    mergeStatus.style.cssText = 'color:#aaa; padding:8px 0; margin:0;';
    content.appendChild(mergeStatus);
}
