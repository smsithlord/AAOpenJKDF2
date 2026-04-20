/* libretroConfig.js — Libretro Configuration Menu */

var extendedInfo = {
    "fbneo_libretro.dll": { systems: "Arcade", notes: "Gold standard, most actively maintained, best compatibility. Sometimes needs BIOS" },
    "mame_libretro.dll": { systems: "Arcade", notes: "Most accurate, very demanding. Sometimes needs BIOS" },
    "mame2010_libretro.dll": { systems: "Arcade", notes: "Good balance of accuracy and performance. Sometimes needs BIOS" },
    "mame2003_plus_libretro.dll": { systems: "Arcade", notes: "Improved 2003 with extra games/fixes. Sometimes needs BIOS" },
    "mame2003_libretro.dll": { systems: "Arcade", notes: "Solid for low-end hardware. Sometimes needs BIOS" },
    "mame2000_libretro.dll": { systems: "Arcade", notes: "Very lightweight but limited compatibility. Sometimes needs BIOS" },
    "genesis_plus_gx_libretro.dll": { systems: "Genesis, SegaCD", notes: "Best overall Genesis/CD core. Sometimes needs BIOS" },
    "genesis_plus_gx_wide_libretro.dll": { systems: "Genesis, SegaCD", notes: "Same as Genesis Plus GX with widescreen hack. Sometimes needs BIOS" },
    "blastem_libretro.dll": { systems: "Genesis", notes: "Very high accuracy, cycle-accurate. No BIOS needed" },
    "picodrive_libretro.dll": { systems: "Genesis, 32x, SegaCD", notes: "Less accurate but fast, only core with 32X support. Sometimes needs BIOS" },
    "bsnes_libretro.dll": { systems: "SNES", notes: "Most accurate, cycle-accurate. No BIOS needed" },
    "bsnes_hd_beta_libretro.dll": { systems: "SNES", notes: "bsnes accuracy with widescreen/HD features. No BIOS needed" },
    "snes9x_libretro.dll": { systems: "SNES", notes: "Best balance of accuracy and performance. No BIOS needed" },
    "mesen_libretro.dll": { systems: "NES", notes: "Most accurate NES emulator. No BIOS needed" },
    "fceumm_libretro.dll": { systems: "NES", notes: "Great compatibility, lots of features. No BIOS needed" },
    "nestopia_libretro.dll": { systems: "NES", notes: "Very good accuracy, cycle-accurate. No BIOS needed" },
    "sameboy_libretro.dll": { systems: "GameBoy", notes: "Most accurate GB/GBC emulator. No BIOS needed" },
    "gambatte_libretro.dll": { systems: "GameBoy", notes: "Excellent accuracy, long-trusted. No BIOS needed" },
    "mgba_libretro.dll": { systems: "GBA", notes: "Best overall: accuracy, features, active development. Sometimes needs BIOS" },
    "vbam_libretro.dll": { systems: "GBA", notes: "Good compatibility, heavier than mGBA. Sometimes needs BIOS" },
    "mednafen_psx_hw_libretro.dll": { systems: "PSX", notes: "Best: high accuracy with GPU enhancements. Needs BIOS" },
    "swanstation_libretro.dll": { systems: "PSX", notes: "Duckstation fork, great accuracy and enhancements. Needs BIOS" },
    "mednafen_psx_libretro.dll": { systems: "PSX", notes: "Very accurate, software rendering only. Needs BIOS" },
    "mupen64plus_next_libretro.dll": { systems: "N64", notes: "Better compatibility, actively maintained. No BIOS needed" },
    "parallel_n64_libretro.dll": { systems: "N64", notes: "Older, less maintained, but still functional. No BIOS needed" },
    "mednafen_saturn_libretro.dll": { systems: "Saturn", notes: "Most accurate, very CPU-demanding. Needs BIOS" },
    "flycast_libretro.dll": { systems: "DC", notes: "Best Dreamcast core, hardware-accelerated rendering. Needs BIOS" },
    "ppsspp_libretro.dll": { systems: "PSP", notes: "Excellent PSP emulator, high compatibility. No BIOS needed" },
    "play_libretro.dll": { systems: "PS2", notes: "Only PS2 Libretro core; limited compatibility. Does NOT support STATE saves. Needs BIOS" }
};

function initLibretroConfig() {
    function onGeneralTab(content) {
        content.innerHTML = '<p style="color:#888; padding:16px;">General settings placeholder.</p>';
    }

    function onCoresTab(content) {
        var cores = [];
        try {
            if (typeof aapi !== 'undefined' && aapi.manager && aapi.manager.getAllLibretroCores)
                cores = aapi.manager.getAllLibretroCores() || [];
        } catch (e) { console.log('getAllLibretroCores error: ' + e); }

        content.innerHTML = '';

        var layout = document.createElement('div');
        layout.style.cssText = 'display:flex; gap:16px; padding:8px; min-height:400px;';

        /* Left: core list */
        var leftCol = document.createElement('div');
        leftCol.style.cssText = 'flex:0 0 280px;';

        var listTitle = document.createElement('div');
        listTitle.style.cssText = 'color:#999; font-size:12px; margin-bottom:4px;';
        listTitle.textContent = 'All Detected Cores';
        leftCol.appendChild(listTitle);

        var dllList = document.createElement('select');
        dllList.id = 'dllList';
        dllList.size = 20;
        dllList.style.cssText = 'width:280px; height:420px; background:#111; color:#ccc; border:1px solid #333; border-radius:6px; font-size:13px; font-family:monospace;';

        for (var i = 0; i < cores.length; i++) {
            var opt = document.createElement('option');
            opt.value = cores[i].file;
            opt.textContent = cores[i].file;
            opt.coreData = cores[i];
            if (cores[i].enabled) {
                opt.style.backgroundColor = '#1a5276';
                opt.style.color = '#fff';
            }
            dllList.appendChild(opt);
        }
        leftCol.appendChild(dllList);
        layout.appendChild(leftCol);

        /* Right: core details */
        var rightCol = document.createElement('div');
        rightCol.style.cssText = 'flex:1; min-width:300px;';

        var coreTitle = document.createElement('div');
        coreTitle.style.cssText = 'color:#4a9eda; font-size:16px; font-weight:bold; margin-bottom:4px;';
        coreTitle.textContent = '- no core selected -';

        var coreSubtitle = document.createElement('div');
        coreSubtitle.style.cssText = 'color:#888; font-size:11px; margin-bottom:12px; line-height:1.4; display:none;';

        var detailsPanel = document.createElement('div');
        detailsPanel.style.cssText = 'display:none;';

        var noCoreMsg = document.createElement('div');
        noCoreMsg.style.cssText = 'color:#888; padding:20px; border:1px solid #333; border-radius:6px; background:#0a0a0a; margin-top:16px;';
        noCoreMsg.innerHTML = 'Select a core from the list to configure its options.<br><br>Cores highlighted in <span style="background:#1a5276; padding:2px 6px; font-weight:bold;">BLUE</span> are currently <b>ENABLED</b>.';

        rightCol.appendChild(coreTitle);
        rightCol.appendChild(coreSubtitle);
        rightCol.appendChild(detailsPanel);
        rightCol.appendChild(noCoreMsg);
        layout.appendChild(rightCol);
        content.appendChild(layout);

        /* Build details panel */
        function createToggle(label, id) {
            var row = document.createElement('div');
            row.style.cssText = 'display:flex; align-items:center; margin-bottom:8px;';
            var lbl = document.createElement('span');
            lbl.style.cssText = 'color:#999; font-weight:bold; width:140px; text-align:right; margin-right:10px;';
            lbl.textContent = label + ':';
            var toggle = document.createElement('div');
            toggle.className = 'toggle-switch';
            toggle.id = id;
            toggle.innerHTML = '<div class="toggle-knob"></div>';
            toggle.curvalue = false;
            toggle.addEventListener('click', function() {
                this.curvalue = !this.curvalue;
                updateToggleVisual(this);
                saveCurrentCore();
            });
            row.appendChild(lbl);
            row.appendChild(toggle);
            return row;
        }

        function updateToggleVisual(toggle) {
            var knob = toggle.querySelector('.toggle-knob');
            if (toggle.curvalue) {
                toggle.classList.add('on');
                knob.classList.add('on');
            } else {
                toggle.classList.remove('on');
                knob.classList.remove('on');
            }
        }

        var enabledRow = createToggle('Enabled', 'enabledToggle');
        var cartRow = createToggle('Auto Cart Saves', 'cartToggle');
        var stateRow = createToggle('Auto State Saves', 'stateToggle');

        detailsPanel.appendChild(enabledRow);
        detailsPanel.appendChild(cartRow);
        detailsPanel.appendChild(stateRow);

        /* Content folders section */
        var foldersHeader = document.createElement('div');
        foldersHeader.style.cssText = 'background:rgba(26,82,118,0.3); text-align:center; padding:8px; margin:12px 0 8px 0; border-radius:4px; color:#4a9eda; font-weight:bold;';
        foldersHeader.textContent = 'Associated Content Folders';
        detailsPanel.appendChild(foldersHeader);

        var noFoldersMsg = document.createElement('div');
        noFoldersMsg.id = 'noFoldersMsg';
        noFoldersMsg.style.cssText = 'text-align:center; color:#888; padding:10px;';
        noFoldersMsg.innerHTML = 'No content folders or file extensions specified yet.<br><br>You must add at least 1 content folder that this core is allowed to run files from for it to work.';
        detailsPanel.appendChild(noFoldersMsg);

        var foldersContainer = document.createElement('div');
        foldersContainer.id = 'foldersContainer';
        detailsPanel.appendChild(foldersContainer);

        var addFolderBtn = document.createElement('button');
        addFolderBtn.textContent = 'Add Another Content Folder';
        addFolderBtn.style.cssText = 'width:100%; padding:8px; margin:8px 0; background:#1a1a1a; color:#ccc; border:1px solid #333; border-radius:4px; cursor:pointer;';
        addFolderBtn.addEventListener('click', function() {
            appendFolderEntry('', '');
            noFoldersMsg.style.display = 'none';
            saveCurrentCore();
        });
        detailsPanel.appendChild(addFolderBtn);

        var resetBtn = document.createElement('button');
        resetBtn.textContent = 'Reset Core Runtime Options';
        resetBtn.style.cssText = 'padding:6px 12px; margin:8px 0; background:#1a1a1a; color:#ccc; border:1px solid #333; border-radius:4px; cursor:pointer;';
        resetBtn.addEventListener('click', function() {
            var sel = dllList.options[dllList.selectedIndex];
            if (!sel) return;
            try {
                if (aapi.manager.resetLibretroCoreOptions)
                    aapi.manager.resetLibretroCoreOptions(sel.value);
            } catch (e) {}
            resetMsg.style.display = 'block';
            setTimeout(function() { resetMsg.style.display = 'none'; }, 2000);
        });
        detailsPanel.appendChild(resetBtn);

        var resetMsg = document.createElement('div');
        resetMsg.style.cssText = 'display:none; color:#4a9eda; font-weight:bold; font-size:12px; padding:4px;';
        resetMsg.textContent = 'Core options reset!';
        detailsPanel.appendChild(resetMsg);

        function appendFolderEntry(path, extensions) {
            var entry = document.createElement('div');
            entry.className = 'folder-entry';
            entry.style.cssText = 'background:#0a0a0a; border:1px solid #222; border-radius:4px; padding:8px; margin-bottom:6px;';

            var r1 = document.createElement('div');
            r1.style.cssText = 'display:flex; align-items:center; margin-bottom:4px;';
            var l1 = document.createElement('span');
            l1.style.cssText = 'color:#999; font-weight:bold; width:120px; font-size:12px;';
            l1.textContent = 'Content Folder:';
            var i1 = document.createElement('input');
            i1.type = 'text';
            i1.value = path;
            i1.placeholder = 'Any (all folders accepted)';
            i1.style.cssText = 'flex:1; background:#080808; color:#ccc; border:1px solid #333; border-radius:4px; padding:4px 6px; font-size:12px;';
            i1.className = 'folder-path';
            r1.appendChild(l1); r1.appendChild(i1);

            var r2 = document.createElement('div');
            r2.style.cssText = 'display:flex; align-items:center; margin-bottom:4px;';
            var l2 = document.createElement('span');
            l2.style.cssText = 'color:#999; font-weight:bold; width:120px; font-size:12px;';
            l2.textContent = 'File Extensions:';
            var i2 = document.createElement('input');
            i2.type = 'text';
            i2.value = extensions;
            i2.placeholder = 'Any (all extensions accepted)';
            i2.style.cssText = 'flex:1; background:#080808; color:#ccc; border:1px solid #333; border-radius:4px; padding:4px 6px; font-size:12px;';
            i2.className = 'folder-ext';
            r2.appendChild(l2); r2.appendChild(i2);

            var r3 = document.createElement('div');
            r3.style.cssText = 'text-align:right;';
            var removeBtn = document.createElement('button');
            removeBtn.textContent = 'Remove Folder';
            removeBtn.style.cssText = 'background:#1a1a1a; color:#999; border:1px solid #333; border-radius:4px; padding:3px 8px; cursor:pointer; font-size:11px; visibility:hidden;';
            removeBtn.addEventListener('click', function() {
                entry.parentNode.removeChild(entry);
                var remaining = foldersContainer.querySelectorAll('.folder-entry');
                if (remaining.length === 0) noFoldersMsg.style.display = 'block';
                saveCurrentCore();
            });
            r3.appendChild(removeBtn);

            entry.addEventListener('mouseenter', function() { removeBtn.style.visibility = 'visible'; });
            entry.addEventListener('mouseleave', function() { removeBtn.style.visibility = 'hidden'; });

            /* Confirm on Enter (green flash + save), revert on blur */
            function setupInputConfirm(input) {
                input.originalValue = input.value;
                input.addEventListener('focus', function() {
                    this.originalValue = this.value;
                });
                input.addEventListener('keydown', function(e) {
                    if (e.key === 'Enter') {
                        this.originalValue = this.value;
                        this.style.transition = 'none';
                        this.style.backgroundColor = '#1a5276';
                        this.offsetTop;
                        this.style.transition = 'background-color 0.5s';
                        this.style.backgroundColor = '#080808';
                        this.blur();
                        saveCurrentCore();
                    }
                });
                input.addEventListener('blur', function() {
                    if (this.value !== this.originalValue)
                        this.value = this.originalValue;
                });
            }
            setupInputConfirm(i1);
            setupInputConfirm(i2);

            entry.appendChild(r1);
            entry.appendChild(r2);
            entry.appendChild(r3);
            foldersContainer.appendChild(entry);
        }

        function saveCurrentCore() {
            var sel = dllList.options[dllList.selectedIndex];
            if (!sel) return;

            var enabledToggle = document.getElementById('enabledToggle');
            var cartToggle = document.getElementById('cartToggle');
            var stateToggle = document.getElementById('stateToggle');

            var paths = [];
            var entries = foldersContainer.querySelectorAll('.folder-entry');
            for (var i = 0; i < entries.length; i++) {
                var p = entries[i].querySelector('.folder-path').value;
                var ext = entries[i].querySelector('.folder-ext').value;
                paths.push({ path: p, extensions: ext });
            }

            var payload = {
                file: sel.value,
                enabled: enabledToggle.curvalue,
                cartSaves: cartToggle.curvalue,
                stateSaves: stateToggle.curvalue,
                priority: 0,
                paths: paths
            };

            /* Update the option highlight */
            if (enabledToggle.curvalue) {
                sel.style.backgroundColor = '#1a5276';
                sel.style.color = '#fff';
            } else {
                sel.style.backgroundColor = '';
                sel.style.color = '';
            }
            sel.coreData = payload;

            try {
                if (aapi.manager.updateLibretroCore)
                    aapi.manager.updateLibretroCore(payload);
            } catch (e) { console.log('updateLibretroCore error: ' + e); }
        }

        dllList.addEventListener('change', function() {
            var sel = this.options[this.selectedIndex];
            if (!sel) return;
            var core = sel.coreData;

            coreTitle.textContent = core.file;
            var info = extendedInfo[core.file];
            if (info) {
                coreSubtitle.innerHTML = '<b>Systems:</b> ' + info.systems + '<br><b>Notes:</b> ' + info.notes;
                coreSubtitle.style.display = 'block';
            } else {
                coreSubtitle.style.display = 'none';
            }

            var enabledToggle = document.getElementById('enabledToggle');
            var cartToggle = document.getElementById('cartToggle');
            var stateToggle = document.getElementById('stateToggle');
            enabledToggle.curvalue = !!core.enabled;
            cartToggle.curvalue = !!core.cartSaves;
            stateToggle.curvalue = !!core.stateSaves;
            updateToggleVisual(enabledToggle);
            updateToggleVisual(cartToggle);
            updateToggleVisual(stateToggle);

            /* Rebuild folder entries */
            foldersContainer.innerHTML = '';
            var paths = core.paths || [];
            if (paths.length === 0) {
                noFoldersMsg.style.display = 'block';
            } else {
                noFoldersMsg.style.display = 'none';
                for (var i = 0; i < paths.length; i++) {
                    appendFolderEntry(paths[i].path || '', paths[i].extensions || '');
                }
            }

            detailsPanel.style.display = 'block';
            noCoreMsg.style.display = 'none';
        });
    }

    function onAboutTab(content) {
        content.innerHTML = '';
        var wrap = document.createElement('div');
        wrap.style.cssText = 'padding:16px; color:#ccc; font-size:13px; line-height:1.6; max-height:500px; overflow-y:auto;';

        wrap.innerHTML = '<div style="text-align:center; margin-bottom:16px;">'
            + '<div style="color:#4a9eda; font-size:20px; font-weight:bold;">Libretro</div>'
            + '</div>'
            + '<p>Libretro is a library programming interface. Programs get ported to this library and can then be run with any libretro-compatible frontend.</p>'
            + '<p>Libretro programs are called <b>cores</b>. They are DLL files that can be embedded into frontends, such as AAOpenJKDF2, and run directly on in-game screens.</p>'
            + '<br>'
            + '<div style="color:#4a9eda; font-size:16px; font-weight:bold;">Addon Cores</div>'
            + '<p><b>NOTE:</b> Addon cores are 3rd party apps so you might have to look up their websites for help or configuration instructions.</p>'
            + '<ol>'
            + '<li style="margin-bottom:8px;"><b>Download Cores From:</b><br>'
            + '<input type="text" readonly value="https://buildbot.libretro.com/nightly/windows/x86_64/latest/" '
            + 'style="width:100%; background:inherit; border:none; color:#ccc; font-size:12px;" onclick="this.select();" /></li>'
            + '<li style="margin-bottom:8px;"><b>Unzip Cores To:</b><br>aarcadecore/libretro/cores</li>'
            + '<li style="margin-bottom:8px;"><b>Enable Cores At Menu:</b><br>Main Menu &gt; Settings &gt; Embedded &gt; Libretro</li>'
            + '<li style="margin-bottom:8px;"><b>Add Content Folders:</b><br>Specify which folders contain files that a core can run.</li>'
            + '</ol>'
            + '<div style="display:none;">'
            + '<br>'
            + '<div style="color:#4a9eda; font-size:16px; font-weight:bold;">Cores That Work</div>'
            + '<p><b>(ARCADE)</b> fbneo_libretro, mame_libretro</p>'
            + '<p><b>(GENESIS)</b> picodrive_libretro, genesis_plus_gx_libretro, blastem_libretro</p>'
            + '<p><b>(SEGA CD)</b> picodrive_libretro, genesis_plus_gx_libretro</p>'
            + '<p><b>(32X)</b> picodrive_libretro</p>'
            + '<p><b>(SNES)</b> bsnes_libretro, bsnes_hd_beta_libretro, snes9x_libretro</p>'
            + '<p><b>(NES)</b> fceumm_libretro, nestopia_libretro, mesen_libretro</p>'
            + '<p><b>(GAMEBOY)</b> gambatte_libretro, sameboy_libretro</p>'
            + '<p><b>(GBA)</b> mgba_libretro, vbam_libretro</p>'
            + '<p><b>(PSX)</b> mednafen_psx_hw_libretro, swanstation_libretro</p>'
            + '<p><b>(N64)</b> mupen64plus_next_libretro, parallel_n64_libretro</p>'
            + '<p><b>(SATURN)</b> mednafen_saturn_libretro</p>'
            + '<p><b>(PSP)</b> ppsspp_libretro</p>'
            + '<br>'
            + '<div style="color:#4a9eda; font-size:16px; font-weight:bold;">Cores That DO NOT Work</div>'
            + '<p>flycast_libretro, yabasanshiro_libretro</p>'
            + '</div>'
            + '<br>'
            + '<div style="color:#4a9eda; font-size:16px; font-weight:bold;">System BIOS</div>'
            + '<p>Some cores expect BIOS to be present. Place them in:</p>'
            + '<p><code>aarcadecore/libretro/system/CORE_NAME_HERE/</code></p>'
            + '<br>'
            + '<div style="color:#4a9eda; font-size:16px; font-weight:bold;">Technical Notes</div>'
            + '<ul>'
            + '<li>AAOpenJKDF2 supports 64-bit Libretro DLLs.</li>'
            + '</ul>';

        content.appendChild(wrap);
    }

    arcadeHud.ui.createWindow({
        title: 'Libretro Config',
        showBack: true,
        showClose: true,
        onBack: function() { window.history.back(); },
        onClose: function() { if (window.aapi && aapi.manager) aapi.manager.closeMenu(); },
        tabs: [
            { label: 'General', onActivate: onGeneralTab },
            { label: 'Cores', onActivate: onCoresTab },
            { label: 'About', onActivate: onAboutTab }
        ]
    });
}
