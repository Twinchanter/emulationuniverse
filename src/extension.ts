import * as vscode from 'vscode';

interface EmulationUniverseSettings {
	general: {
		romLibraryLocation: string;
		saveStateStorageLocation: string;
		autoSave: boolean;
		autoLoad: boolean;
		language: string;
		theme: 'light' | 'dark' | 'custom';
		updateChannel: 'stable' | 'preview' | 'nightly';
	};
	video: {
		renderer: 'OpenGL' | 'Vulkan' | 'DirectX';
		internalResolution: string;
		aspectRatio: string;
		vsync: boolean;
	};
	audio: {
		outputDevice: string;
		volume: number;
		latencyMs: number;
		backend: string;
		audioSync: boolean;
	};
	automation: {
		runTaskLabel: string;
		debugTaskLabel: string;
	};
}

const defaultSettings: EmulationUniverseSettings = {
	general: {
		romLibraryLocation: 'roms/gameboy',
		saveStateStorageLocation: 'states',
		autoSave: true,
		autoLoad: false,
		language: 'en-US',
		theme: 'dark',
		updateChannel: 'stable'
	},
	video: {
		renderer: 'OpenGL',
		internalResolution: 'native',
		aspectRatio: '4:3',
		vsync: true
	},
	audio: {
		outputDevice: 'default',
		volume: 100,
		latencyMs: 60,
		backend: 'SDL',
		audioSync: true
	},
	automation: {
		runTaskLabel: 'Run Pokemon Red',
		debugTaskLabel: 'Run Pokemon Debug Script'
	}
};

class SettingsStore {
	private static readonly relativeDir = '.emulationuniverse';
	private static readonly relativeFile = '.emulationuniverse/settings.json';

	public async load(): Promise<EmulationUniverseSettings> {
		const uri = this.getSettingsFileUri();
		if (!uri) {
			return this.cloneDefaults();
		}

		try {
			const bytes = await vscode.workspace.fs.readFile(uri);
			const text = this.decodeBytes(bytes);
			const parsed = JSON.parse(text) as Partial<EmulationUniverseSettings>;
			return this.mergeDefaults(parsed);
		} catch {
			return this.cloneDefaults();
		}
	}

	public async save(settings: EmulationUniverseSettings): Promise<void> {
		const folder = this.getWorkspaceFolderUri();
		if (!folder) {
			throw new Error('No workspace folder is open.');
		}

		const dirUri = vscode.Uri.joinPath(folder, SettingsStore.relativeDir);
		await vscode.workspace.fs.createDirectory(dirUri);

		const fileUri = vscode.Uri.joinPath(folder, SettingsStore.relativeFile);
		const content = JSON.stringify(settings, null, 2);
		await vscode.workspace.fs.writeFile(fileUri, this.encodeString(content));
	}

	public getSettingsFileFsPath(): string | undefined {
		return this.getSettingsFileUri()?.fsPath;
	}

	public getSettingsFileUri(): vscode.Uri | undefined {
		const folder = this.getWorkspaceFolderUri();
		if (!folder) {
			return undefined;
		}
		return vscode.Uri.joinPath(folder, SettingsStore.relativeFile);
	}

	private getWorkspaceFolderUri(): vscode.Uri | undefined {
		return vscode.workspace.workspaceFolders?.[0]?.uri;
	}

	private mergeDefaults(parsed: Partial<EmulationUniverseSettings>): EmulationUniverseSettings {
		return {
			general: {
				...defaultSettings.general,
				...(parsed.general ?? {})
			},
			video: {
				...defaultSettings.video,
				...(parsed.video ?? {})
			},
			audio: {
				...defaultSettings.audio,
				...(parsed.audio ?? {})
			},
			automation: {
				...defaultSettings.automation,
				...(parsed.automation ?? {})
			}
		};
	}

	private cloneDefaults(): EmulationUniverseSettings {
		return JSON.parse(JSON.stringify(defaultSettings)) as EmulationUniverseSettings;
	}

	private decodeBytes(bytes: Uint8Array): string {
		let result = '';
		for (let i = 0; i < bytes.length; i++) {
			result += String.fromCharCode(bytes[i]);
		}
		return result;
	}

	private encodeString(text: string): Uint8Array {
		const out = new Uint8Array(text.length);
		for (let i = 0; i < text.length; i++) {
			out[i] = text.charCodeAt(i);
		}
		return out;
	}
}

export function activate(context: vscode.ExtensionContext) {
	const settingsStore = new SettingsStore();
	let controlCenterPanel: vscode.WebviewPanel | undefined;
	const activeRomKey = 'emulationUniverse.activeRomPath';
	const debugSessionKey = 'emulationUniverse.debugSessionActive';
	const activeDebugExecutions = new Set<vscode.TaskExecution>();

	async function getAutomationTaskLabels(): Promise<{ runTaskLabel: string; debugTaskLabel: string }> {
		const settings = await settingsStore.load();
		return {
			runTaskLabel: settings.automation.runTaskLabel || defaultSettings.automation.runTaskLabel,
			debugTaskLabel: settings.automation.debugTaskLabel || defaultSettings.automation.debugTaskLabel
		};
	}

	async function validateAutomationTaskLabels(labels: { runTaskLabel: string; debugTaskLabel: string }): Promise<{ missing: string[] }> {
		const tasks = await vscode.tasks.fetchTasks();
		const names = new Set(tasks.map(task => task.name));
		const missing: string[] = [];

		if (!names.has(labels.runTaskLabel)) {
			missing.push(labels.runTaskLabel);
		}
		if (!names.has(labels.debugTaskLabel)) {
			missing.push(labels.debugTaskLabel);
		}

		return { missing };
	}

	const commandLabels: Record<string, string> = {
		'emulationUniverse.file.loadRom': 'Load ROM',
		'emulationUniverse.file.recentGames': 'Launch Recent ROM',
		'emulationUniverse.emulation.pause': 'Pause/Resume',
		'emulationUniverse.emulation.reset': 'Reset Emulation',
		'emulationUniverse.emulation.fastForward': 'Fast Forward',
		'emulationUniverse.emulation.saveState': 'Save State',
		'emulationUniverse.emulation.loadState': 'Load State',
		'emulationUniverse.input.controllerSetup': 'Open Controller Setup',
		'emulationUniverse.input.scanControllers': 'Scan Controllers',
		'emulationUniverse.view.fullscreen': 'Toggle Fullscreen',
		'emulationUniverse.debug.cpuDebugger': 'Open CPU Debugger',
		'emulationUniverse.debug.stopSession': 'Stop Debug Session',
		'emulationUniverse.help.documentation': 'Open Documentation',
		'emulationUniverse.help.about': 'Show About',
		'emulationUniverse.settings.openFile': 'Open Settings JSON'
	};

	function commandLabel(commandId: string): string {
		return commandLabels[commandId] ?? commandId;
	}

	async function updateActiveRom(romPath: string): Promise<void> {
		await context.globalState.update(activeRomKey, romPath);
		controlCenterPanel?.webview.postMessage({
			type: 'romState',
			loaded: true,
			romPath
		});
	}

	async function updateDebugSession(active: boolean): Promise<void> {
		await context.globalState.update(debugSessionKey, active);
		controlCenterPanel?.webview.postMessage({
			type: 'debugState',
			active
		});
	}

	const runPokemonRedCommand = vscode.commands.registerCommand('emulationUniverse.emulator.runPokemonRed', async () => {
		const labels = await getAutomationTaskLabels();
		await runTaskByLabel(labels.runTaskLabel);
		await updateDebugSession(false);
		const folder = vscode.workspace.workspaceFolders?.[0]?.uri;
		if (folder) {
			const defaultRomUri = vscode.Uri.joinPath(folder, 'roms', 'gameboy', 'pokemon_red_sgb.gb');
			try {
				await vscode.workspace.fs.stat(defaultRomUri);
				await updateActiveRom(defaultRomUri.fsPath);
			} catch {
				// If the expected default ROM is unavailable, keep current active ROM state unchanged.
			}
		}
	});

	const runPokemonDebugCommand = vscode.commands.registerCommand('emulationUniverse.emulator.runPokemonDebugProfile', async () => {
		const labels = await getAutomationTaskLabels();
		await runTaskByLabel(labels.debugTaskLabel);
		const folder = vscode.workspace.workspaceFolders?.[0]?.uri;
		if (folder) {
			const defaultRomUri = vscode.Uri.joinPath(folder, 'roms', 'gameboy', 'pokemon_red_sgb.gb');
			try {
				await vscode.workspace.fs.stat(defaultRomUri);
				await updateActiveRom(defaultRomUri.fsPath);
			} catch {
				// If the expected default ROM is unavailable, keep current active ROM state unchanged.
			}
		}
	});

	const openSettingsFileCommand = vscode.commands.registerCommand('emulationUniverse.settings.openFile', async () => {
		const settings = await settingsStore.load();
		await settingsStore.save(settings);

		const uri = settingsStore.getSettingsFileUri();
		if (!uri) {
			vscode.window.showWarningMessage('No workspace folder is open to store settings.');
			return;
		}

		const doc = await vscode.workspace.openTextDocument(uri);
		await vscode.window.showTextDocument(doc, { preview: false });
	});

	const fileLoadRomCommand = vscode.commands.registerCommand('emulationUniverse.file.loadRom', async () => {
		const picked = await vscode.window.showOpenDialog({
			canSelectMany: false,
			filters: { 'Game ROMs': ['gb', 'gbc', 'gba', 'bin', 'iso'] },
			title: 'Select a ROM or Disc Image'
		});
		if (!picked || picked.length === 0) {
			return;
		}

		await runEmulatorWithRom(picked[0].fsPath);
	});

	const fileRecentGamesCommand = vscode.commands.registerCommand('emulationUniverse.file.recentGames', async () => {
		const recent = context.globalState.get<string[]>('emulationUniverse.recentRoms', []);
		if (recent.length === 0) {
			vscode.window.showInformationMessage('No recent ROMs yet. Load a ROM first.');
			return;
		}

		const choice = await vscode.window.showQuickPick(recent, {
			placeHolder: 'Select a recent ROM to launch'
		});
		if (!choice) return;
		await runEmulatorWithRom(choice);
	});

	const emulationResetCommand = vscode.commands.registerCommand('emulationUniverse.emulation.reset', async () => {
		const labels = await getAutomationTaskLabels();
		await runTaskByLabel(labels.runTaskLabel);
		await updateDebugSession(false);
	});

	const emulationPauseCommand = vscode.commands.registerCommand('emulationUniverse.emulation.pause', async () => {
		vscode.window.showInformationMessage('Pause/Resume toggled. (Hook this to runtime IPC in next pass.)');
	});

	const emulationFastForwardCommand = vscode.commands.registerCommand('emulationUniverse.emulation.fastForward', async () => {
		vscode.window.showInformationMessage('Fast-forward enabled. (Runtime speed multiplier wiring pending.)');
	});

	const emulationSaveStateCommand = vscode.commands.registerCommand('emulationUniverse.emulation.saveState', async () => {
		vscode.window.showInformationMessage('Use --save-state from run profile; direct runtime save-state trigger is next integration step.');
	});

	const emulationLoadStateCommand = vscode.commands.registerCommand('emulationUniverse.emulation.loadState', async () => {
		vscode.window.showInformationMessage('Use --load-state from run profile; direct runtime load-state trigger is next integration step.');
	});

	const inputControllerSetupCommand = vscode.commands.registerCommand('emulationUniverse.input.controllerSetup', async () => {
		vscode.window.showInformationMessage('Controller Setup panel selected in Control Center.');
	});

	const inputScanControllersCommand = vscode.commands.registerCommand('emulationUniverse.input.scanControllers', async () => {
		const labels = await getAutomationTaskLabels();
		await runTaskByLabel(labels.debugTaskLabel);
	});

	const viewFullscreenCommand = vscode.commands.registerCommand('emulationUniverse.view.fullscreen', async () => {
		await vscode.commands.executeCommand('workbench.action.toggleFullScreen');
	});

	const debugCpuCommand = vscode.commands.registerCommand('emulationUniverse.debug.cpuDebugger', async () => {
		const labels = await getAutomationTaskLabels();
		await runTaskByLabel(labels.debugTaskLabel);
	});

	const debugStopSessionCommand = vscode.commands.registerCommand('emulationUniverse.debug.stopSession', async () => {
		await updateDebugSession(false);
		vscode.window.showInformationMessage('Debug session marked as stopped in Control Center.');
	});

	const helpDocsCommand = vscode.commands.registerCommand('emulationUniverse.help.documentation', async () => {
		await vscode.commands.executeCommand('vscode.open', vscode.Uri.file(`${vscode.workspace.workspaceFolders?.[0]?.uri.fsPath ?? ''}/README.md`));
	});

	const helpAboutCommand = vscode.commands.registerCommand('emulationUniverse.help.about', async () => {
		vscode.window.showInformationMessage('EmulationUniverse Control Center: Professional emulator UX + debug/test tooling inside VS Code.');
	});

	const controlCenterCommand = vscode.commands.registerCommand('emulationUniverse.ui.openControlCenter', () => {
		const panel = vscode.window.createWebviewPanel(
			'emulationUniverseControlCenter',
			'EmulationUniverse Control Center',
			vscode.ViewColumn.One,
			{ enableScripts: true }
		);
		controlCenterPanel = panel;
		panel.onDidDispose(() => {
			if (controlCenterPanel === panel) {
				controlCenterPanel = undefined;
			}
		}, undefined, context.subscriptions);

		panel.webview.html = renderControlCenterHtml(panel.webview);

		panel.webview.onDidReceiveMessage(async (message: unknown) => {
			const msg = message as {
				type?: string;
				command?: string;
				text?: string;
				settings?: EmulationUniverseSettings;
			};

			if (!msg?.type) {
				return;
			}

			if (msg.type === 'ready' || msg.type === 'loadSettings') {
				const settings = await settingsStore.load();
				const activeRomPath = context.globalState.get<string>(activeRomKey);
				const debugActive = context.globalState.get<boolean>(debugSessionKey, false);
				const validation = await validateAutomationTaskLabels(settings.automation);
				panel.webview.postMessage({
					type: 'settingsLoaded',
					settings,
					settingsPath: settingsStore.getSettingsFileFsPath() ?? 'No workspace folder'
				});
				panel.webview.postMessage({
					type: 'romState',
					loaded: Boolean(activeRomPath),
					romPath: activeRomPath ?? ''
				});
				panel.webview.postMessage({
					type: 'debugState',
					active: debugActive
				});
				panel.webview.postMessage({
					type: 'automationValidation',
					missing: validation.missing
				});
				return;
			}

			if (msg.type === 'saveSettings' && msg.settings) {
				try {
					await settingsStore.save(msg.settings);
					const validation = await validateAutomationTaskLabels(msg.settings.automation);
					vscode.window.showInformationMessage('EmulationUniverse settings saved.');
					panel.webview.postMessage({ type: 'settingsSaved' });
					panel.webview.postMessage({ type: 'actionStatus', level: 'success', text: 'Settings saved.' });
					panel.webview.postMessage({ type: 'automationValidation', missing: validation.missing });
					if (validation.missing.length > 0) {
						panel.webview.postMessage({
							type: 'actionStatus',
							level: 'error',
							text: `Missing tasks for automation labels: ${validation.missing.join(', ')}`
						});
					}
				} catch (error) {
					vscode.window.showErrorMessage(`Failed to save settings: ${String(error)}`);
					panel.webview.postMessage({ type: 'actionStatus', level: 'error', text: `Failed to save settings: ${String(error)}` });
				}
				return;
			}

			if (msg.type === 'executeCommand' && msg.command) {
				try {
					await vscode.commands.executeCommand(msg.command);
					panel.webview.postMessage({ type: 'actionStatus', level: 'success', text: `${commandLabel(msg.command)} completed.` });
				} catch (error) {
					const messageText = `${commandLabel(msg.command)} failed: ${String(error)}`;
					vscode.window.showErrorMessage(messageText);
					panel.webview.postMessage({ type: 'actionStatus', level: 'error', text: messageText });
				}
				return;
			}

			if (msg.type === 'showInfo' && msg.text) {
				vscode.window.showInformationMessage(msg.text);
			}
		}, undefined, context.subscriptions);
	});

	const writeEmitter = new vscode.EventEmitter<string>();
	context.subscriptions.push(vscode.commands.registerCommand('emulationUniverse.terminal.create', () => {
		let line = '';
		const pty = {
			onDidWrite: writeEmitter.event,
			open: () => writeEmitter.fire('Type and press enter to echo the text\r\n\r\n'),
			close: () => { /* noop*/ },
			handleInput: (data: string) => {
				if (data === '\r') { // Enter
					writeEmitter.fire(`\r\necho: "${colorText(line)}"\r\n\n`);
					line = '';
					return;
				}
				if (data === '\x7f') { // Backspace
					if (line.length === 0) {
						return;
					}
					line = line.substr(0, line.length - 1);
					// Move cursor backward
					writeEmitter.fire('\x1b[D');
					// Delete character
					writeEmitter.fire('\x1b[P');
					return;
				}
				line += data;
				writeEmitter.fire(data);
			}
		};
		const terminal = vscode.window.createTerminal({ name: `My Extension REPL`, pty });
		terminal.show();
	}));

	context.subscriptions.push(vscode.commands.registerCommand('emulationUniverse.terminal.clear', () => {
		writeEmitter.fire('\x1b[2J\x1b[3J\x1b[;H');
	}));

	context.subscriptions.push(controlCenterCommand);
	context.subscriptions.push(runPokemonRedCommand);
	context.subscriptions.push(runPokemonDebugCommand);
	context.subscriptions.push(openSettingsFileCommand);
	context.subscriptions.push(fileLoadRomCommand);
	context.subscriptions.push(fileRecentGamesCommand);
	context.subscriptions.push(emulationResetCommand);
	context.subscriptions.push(emulationPauseCommand);
	context.subscriptions.push(emulationFastForwardCommand);
	context.subscriptions.push(emulationSaveStateCommand);
	context.subscriptions.push(emulationLoadStateCommand);
	context.subscriptions.push(inputControllerSetupCommand);
	context.subscriptions.push(inputScanControllersCommand);
	context.subscriptions.push(viewFullscreenCommand);
	context.subscriptions.push(debugCpuCommand);
	context.subscriptions.push(debugStopSessionCommand);
	context.subscriptions.push(helpDocsCommand);
	context.subscriptions.push(helpAboutCommand);

	context.subscriptions.push(vscode.tasks.onDidStartTaskProcess(async event => {
		const labels = await getAutomationTaskLabels();
		const label = event.execution.task.name;
		if (label === labels.debugTaskLabel) {
			activeDebugExecutions.add(event.execution);
			await updateDebugSession(true);
			return;
		}

		if (label === labels.runTaskLabel) {
			activeDebugExecutions.clear();
			await updateDebugSession(false);
		}
	}));

	context.subscriptions.push(vscode.tasks.onDidEndTaskProcess(async event => {
		const labels = await getAutomationTaskLabels();
		const label = event.execution.task.name;
		if (label !== labels.debugTaskLabel) {
			return;
		}

		activeDebugExecutions.delete(event.execution);
		if (activeDebugExecutions.size === 0) {
			await updateDebugSession(false);
		}
	}));

	async function runEmulatorWithRom(romPath: string): Promise<void> {
		const folder = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
		if (!folder) {
			vscode.window.showWarningMessage('No workspace folder is open.');
			return;
		}

		const exe = vscode.Uri.file(folder).fsPath + '/build/Release/emulationuniverse.exe';
		const terminal = vscode.window.createTerminal('EmulationUniverse Launcher');
		terminal.show();
		terminal.sendText(`"${exe}" "${romPath}"`);

		const current = context.globalState.get<string[]>('emulationUniverse.recentRoms', []);
		const deduped = [romPath, ...current.filter(x => x !== romPath)].slice(0, 12);
		await context.globalState.update('emulationUniverse.recentRoms', deduped);
		await updateActiveRom(romPath);
		await updateDebugSession(false);
	}
}

async function runTaskByLabel(label: string): Promise<void> {
	const tasks = await vscode.tasks.fetchTasks();
	const task = tasks.find(t => t.name === label);
	if (!task) {
		vscode.window.showWarningMessage(`Task '${label}' not found. Check .vscode/tasks.json.`);
		return;
	}
	await vscode.tasks.executeTask(task);
}

function colorText(text: string): string {
	let output = '';
	let colorIndex = 1;
	for (let i = 0; i < text.length; i++) {
		const char = text.charAt(i);
		if (char === ' ' || char === '\r' || char === '\n') {
			output += char;
		} else {
			output += `\x1b[3${colorIndex++}m${text.charAt(i)}\x1b[0m`;
			if (colorIndex > 6) {
				colorIndex = 1;
			}
		}
	}
	return output;
}

function renderControlCenterHtml(webview: vscode.Webview): string {
	const nonce = getNonce();

	return `<!DOCTYPE html>
<html lang="en">
<head>
	<meta charset="UTF-8" />
	<meta name="viewport" content="width=device-width, initial-scale=1.0" />
	<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'; script-src 'nonce-${nonce}';" />
	<title>EmulationUniverse Control Center</title>
	<style>
		:root {
			--bg-0: #0b1017;
			--bg-1: #131a24;
			--bg-2: #1b2431;
			--line: #33445c;
			--text-strong: #eef3ff;
			--text-soft: #a9b7cc;
			--text-dim: #8d9bb0;
			--accent: #2dd4bf;
			--accent-2: #f59e0b;
			--danger: #f97316;
		}

		* {
			box-sizing: border-box;
		}

		body {
			margin: 0;
			font-family: "Segoe UI Variable", "Bahnschrift", "Trebuchet MS", sans-serif;
			color: var(--text-strong);
			background:
				radial-gradient(1200px 800px at -10% 0%, #12304a 0%, transparent 55%),
				radial-gradient(1000px 700px at 110% 10%, #1b3040 0%, transparent 50%),
				linear-gradient(145deg, var(--bg-0), #101827 52%, #0d1420);
			min-height: 100vh;
			overflow: hidden;
		}

		.shell {
			display: grid;
			grid-template-rows: auto auto 1fr;
			height: 100vh;
			animation: fadeIn 420ms ease-out;
		}

		@keyframes fadeIn {
			from { opacity: 0; transform: translateY(6px); }
			to { opacity: 1; transform: translateY(0); }
		}

		.topbar {
			display: flex;
			align-items: center;
			justify-content: space-between;
			padding: 10px 14px;
			border-bottom: 1px solid var(--line);
			background: linear-gradient(180deg, #152133, #121c2c);
		}

		.brand {
			display: flex;
			align-items: center;
			gap: 10px;
		}

		.brand .badge {
			padding: 3px 8px;
			font-size: 11px;
			border-radius: 12px;
			background: #17314a;
			color: #7de4d4;
			border: 1px solid #2b556f;
		}

		.brand h1 {
			margin: 0;
			font-size: 17px;
			font-weight: 650;
			letter-spacing: 0.2px;
		}

		.quick-actions {
			display: flex;
			gap: 8px;
		}

		button {
			border: 1px solid #33547a;
			background: linear-gradient(180deg, #1b3656, #132b44);
			color: #d8e8ff;
			border-radius: 8px;
			padding: 6px 10px;
			font-size: 12px;
			cursor: pointer;
		}

		button:hover {
			border-color: #4e7db0;
			filter: brightness(1.06);
		}

		button:disabled {
			opacity: 0.55;
			cursor: not-allowed;
			filter: none;
			border-color: #2a3c53;
		}

		.menubar {
			display: flex;
			align-items: center;
			gap: 6px;
			padding: 8px 10px;
			border-bottom: 1px solid var(--line);
			background: #0f1824;
		}

		.menu-item {
			padding: 8px 11px;
			font-size: 12px;
			border: 1px solid transparent;
			border-radius: 8px;
			background: transparent;
			color: var(--text-soft);
			cursor: pointer;
		}

		.menu-item:hover {
			color: var(--text-strong);
			background: #1a2738;
			border-color: #31445d;
		}

		.menu-item.active {
			color: #082521;
			background: linear-gradient(135deg, #2dd4bf, #7dd3fc);
			border-color: #7de8da;
			font-weight: 650;
		}

		.layout {
			display: grid;
			grid-template-columns: 260px 1fr;
			height: 100%;
			min-height: 0;
		}

		.sidebar {
			border-right: 1px solid var(--line);
			padding: 12px;
			background: linear-gradient(180deg, #0f1824, #0e1622);
			overflow-y: auto;
		}

		.sidebar h2 {
			font-size: 12px;
			font-weight: 650;
			color: var(--text-dim);
			text-transform: uppercase;
			letter-spacing: 0.85px;
			margin: 12px 4px 8px;
		}

		.tab {
			display: block;
			width: 100%;
			text-align: left;
			padding: 9px 10px;
			margin-bottom: 6px;
			border-radius: 8px;
			border: 1px solid #243343;
			background: #131e2d;
			color: var(--text-soft);
			font-size: 13px;
		}

		.tab.active {
			color: #082521;
			background: linear-gradient(135deg, #2dd4bf, #7dd3fc);
			border-color: #7de8da;
			font-weight: 650;
		}

		.main {
			padding: 14px;
			overflow-y: auto;
			min-height: 0;
		}

		.panel {
			display: none;
			animation: paneIn 200ms ease-out;
		}

		.panel.active {
			display: block;
		}

		@keyframes paneIn {
			from { opacity: 0; transform: translateX(6px); }
			to { opacity: 1; transform: translateX(0); }
		}

		.panel-head {
			display: flex;
			align-items: center;
			justify-content: space-between;
			margin-bottom: 12px;
		}

		.panel-head h3 {
			margin: 0;
			font-size: 20px;
			font-weight: 650;
		}

		.grid {
			display: grid;
			grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
			gap: 12px;
		}

		.card {
			padding: 12px;
			border-radius: 12px;
			background: linear-gradient(170deg, #131f2f, #121a25);
			border: 1px solid #2b3f57;
		}

		.card h4 {
			margin: 0 0 8px;
			font-size: 14px;
			font-weight: 650;
		}

		.card ul {
			margin: 0;
			padding-left: 18px;
			color: var(--text-soft);
			font-size: 12px;
			line-height: 1.7;
		}

		.form-grid {
			display: grid;
			grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
			gap: 10px;
		}

		.field {
			display: flex;
			flex-direction: column;
			gap: 5px;
		}

		.field label {
			font-size: 11px;
			text-transform: uppercase;
			letter-spacing: 0.7px;
			color: var(--text-dim);
		}

		.field input,
		.field select {
			border: 1px solid #355070;
			background: #0f1a27;
			color: var(--text-strong);
			padding: 7px 9px;
			border-radius: 8px;
			font-size: 12px;
		}

		.actions {
			display: flex;
			gap: 8px;
			margin-top: 10px;
			flex-wrap: wrap;
		}

		.panel-actions {
			display: flex;
			gap: 8px;
			margin-top: 10px;
			flex-wrap: wrap;
		}

		.status {
			margin-top: 8px;
			font-size: 11px;
			color: var(--text-dim);
		}

		.token {
			display: inline-block;
			padding: 2px 7px;
			border-radius: 999px;
			font-size: 11px;
			font-weight: 650;
			margin-right: 6px;
		}

		.token.good { background: #123a33; color: #75e8d6; border: 1px solid #2d6f64; }
		.token.warn { background: #3a2a12; color: #facc74; border: 1px solid #7f5b2c; }
		.token.alert { background: #40251a; color: #fdba74; border: 1px solid #8a4a2b; }
		.token.idle { background: #1e2937; color: #a8bed8; border: 1px solid #38506b; }

		.footer-note {
			margin-top: 14px;
			color: var(--text-dim);
			font-size: 11px;
		}

		.toast {
			position: fixed;
			right: 16px;
			bottom: 16px;
			max-width: 380px;
			padding: 9px 12px;
			border-radius: 8px;
			border: 1px solid #2f435b;
			background: #132234;
			color: var(--text-strong);
			font-size: 12px;
			opacity: 0;
			transform: translateY(6px);
			pointer-events: none;
			transition: opacity 160ms ease, transform 160ms ease;
		}

		.toast.show {
			opacity: 1;
			transform: translateY(0);
		}

		.toast.success {
			border-color: #2d6f64;
			background: #123a33;
			color: #d9fff8;
		}

		.toast.error {
			border-color: #8a4a2b;
			background: #40251a;
			color: #ffe7d8;
		}

		@media (max-width: 980px) {
			.layout {
				grid-template-columns: 1fr;
			}

			.sidebar {
				border-right: none;
				border-bottom: 1px solid var(--line);
				max-height: 34vh;
			}
		}
	</style>
</head>
<body>
	<div class="shell">
		<div class="topbar">
			<div class="brand">
				<span class="badge">Control Center</span>
				<h1>EmulationUniverse Pro UX Blueprint</h1>
				<div><span id="rom-state-chip" class="token idle">ROM: Not Loaded</span></div>
				<div><span id="debug-state-chip" class="token idle">Debug: Inactive</span></div>
			</div>
			<div class="quick-actions">
				<button data-command="emulationUniverse.emulator.runPokemonRed">Launch Game</button>
				<button id="btn-open-settings">Open Settings</button>
				<button data-command="emulationUniverse.emulator.runPokemonDebugProfile">Scan Controllers</button>
			</div>
		</div>

		<div class="menubar" aria-label="Top-level emulator menus">
			<button class="menu-item" data-tab-target="file" data-command="emulationUniverse.file.loadRom">File</button>
			<button class="menu-item" data-tab-target="emulation" data-command="emulationUniverse.emulation.pause">Emulation</button>
			<button class="menu-item" data-tab-target="view" data-command="emulationUniverse.view.fullscreen">View</button>
			<button class="menu-item" data-tab-target="input" data-command="emulationUniverse.input.controllerSetup">Input</button>
			<button class="menu-item" data-tab-target="debug" data-command="emulationUniverse.debug.cpuDebugger">Tools</button>
			<button class="menu-item" data-tab-target="general">Settings</button>
			<button class="menu-item" data-tab-target="help" data-command="emulationUniverse.help.documentation">Help</button>
		</div>

		<div class="layout">
			<aside class="sidebar">
				<h2>Top-Level Menus</h2>
				<button class="tab active" data-tab="file">File</button>
				<button class="tab" data-tab="emulation">Emulation</button>
				<button class="tab" data-tab="input">Input</button>
				<button class="tab" data-tab="view">View</button>
				<button class="tab" data-tab="settings-shell">Settings</button>
				<button class="tab" data-tab="debug">Debug</button>
				<button class="tab" data-tab="help">Help</button>

				<h2>Settings Tabs</h2>
				<button class="tab" data-tab="general">General</button>
				<button class="tab" data-tab="input-settings">Input Settings</button>
				<button class="tab" data-tab="audio">Audio</button>
				<button class="tab" data-tab="video">Video</button>
				<button class="tab" data-tab="network">Network</button>
				<button class="tab" data-tab="storage">Storage & Data</button>
				<button class="tab" data-tab="patches">Patches & Mods</button>
				<button class="tab" data-tab="advanced">Advanced</button>
				<button class="tab" data-tab="theme">UI / Theme</button>
			</aside>

			<main class="main">
				<section class="panel active" data-panel="file">
					<div class="panel-head">
						<h3>File</h3>
						<div><span class="token good">Core</span></div>
					</div>
					<div class="grid">
						<div class="card"><h4>ROM/Disc</h4><ul><li>Load ROM or Disc Image</li><li>Load BIOS</li><li>Recent Games</li></ul><div class="panel-actions"><button data-command="emulationUniverse.file.loadRom">Load ROM</button><button data-command="emulationUniverse.file.recentGames">Recent Games</button></div></div>
						<div class="card"><h4>Saves</h4><ul><li>Import Save Files</li><li>Export Save Files</li><li>Save Migration Tools</li></ul></div>
						<div class="card"><h4>Session</h4><ul><li>New Session</li><li>Close Game</li><li>Quit</li></ul></div>
					</div>
				</section>

				<section class="panel" data-panel="emulation">
					<div class="panel-head"><h3>Emulation</h3><div><span class="token good">Runtime</span></div></div>
					<div class="grid">
						<div class="card"><h4>Core Control</h4><ul><li>Start / Pause</li><li>Reset</li><li>Fast-Forward</li><li>Rewind</li></ul><div class="panel-actions"><button data-command="emulationUniverse.emulation.pause" data-requires-rom="true">Pause/Resume</button><button data-command="emulationUniverse.emulation.reset" data-requires-rom="true">Reset</button><button data-command="emulationUniverse.emulation.fastForward" data-requires-rom="true">Fast Forward</button></div></div>
						<div class="card"><h4>State Tools</h4><ul><li>Save State</li><li>Load State</li><li>Save State Manager</li></ul><div class="panel-actions"><button data-command="emulationUniverse.emulation.saveState" data-requires-rom="true">Save State</button><button data-command="emulationUniverse.emulation.loadState" data-requires-rom="true">Load State</button></div></div>
						<div class="card"><h4>Gameplay Ops</h4><ul><li>Cheat Toggle</li><li>Cheat Profiles</li><li>Frame Advance</li></ul></div>
					</div>
				</section>

				<section class="panel" data-panel="input">
					<div class="panel-head"><h3>Input</h3><div><span class="token good">Controllers</span></div></div>
					<div class="grid">
						<div class="card"><h4>Device Setup</h4><ul><li>Controller Setup</li><li>Keyboard Setup</li><li>Player Assignment</li></ul><div class="panel-actions"><button data-command="emulationUniverse.input.controllerSetup">Controller Setup</button></div></div>
						<div class="card"><h4>Device Health</h4><ul><li>Connected Controller Scan</li><li>Deadzone & Sensitivity</li><li>Rumble / Haptics</li></ul><div class="panel-actions"><button data-command="emulationUniverse.input.scanControllers" data-requires-rom="true">Scan Controllers</button></div></div>
					</div>
				</section>

				<section class="panel" data-panel="view">
					<div class="panel-head"><h3>View</h3><div><span class="token good">Rendering</span></div></div>
					<div class="grid">
						<div class="card"><h4>Display</h4><ul><li>Fullscreen</li><li>Window Size Presets</li><li>Aspect / Scaling Presets</li></ul><div class="panel-actions"><button data-command="emulationUniverse.view.fullscreen">Toggle Fullscreen</button></div></div>
						<div class="card"><h4>Panels & Filters</h4><ul><li>Toggle UI Panels</li><li>Shader Selection</li><li>On-screen Stats</li></ul></div>
					</div>
				</section>

				<section class="panel" data-panel="settings-shell">
					<div class="panel-head"><h3>Settings Shell</h3><div><span class="token good">Tabs</span></div></div>
					<div class="card"><h4>Top-tier Settings Tab Order</h4><ul><li>General</li><li>Input</li><li>Audio</li><li>Video</li><li>Network</li><li>Storage</li><li>Patches & Mods</li><li>Advanced</li><li>UI/Theme</li></ul></div>
				</section>

				<section class="panel" data-panel="debug">
					<div class="panel-head"><h3>Debug</h3><div><span class="token alert">Engineering</span></div></div>
					<div class="grid">
						<div class="card"><h4>CPU</h4><ul><li>CPU Debugger</li><li>Breakpoints</li><li>Trace Logger</li></ul><div class="panel-actions"><button data-command="emulationUniverse.emulator.runPokemonDebugProfile" data-requires-rom="true">Start Debug Session</button><button data-command="emulationUniverse.debug.cpuDebugger" data-requires-debug="true">Open CPU Debugger</button><button data-command="emulationUniverse.debug.stopSession" data-requires-debug="true">Stop Debug Session</button></div></div>
						<div class="card"><h4>Memory + Video + Audio</h4><ul><li>Memory Viewer</li><li>VRAM/PPU Viewer</li><li>Audio Channel Viewer</li></ul></div>
					</div>
				</section>

				<section class="panel" data-panel="help">
					<div class="panel-head"><h3>Help</h3><div><span class="token warn">Support</span></div></div>
					<div class="grid">
						<div class="card"><h4>Guidance</h4><ul><li>About</li><li>Documentation</li><li>System Requirements</li><li>Check for Updates</li></ul><div class="panel-actions"><button data-command="emulationUniverse.help.documentation">Documentation</button><button data-command="emulationUniverse.help.about">About</button></div></div>
					</div>
				</section>

				<section class="panel" data-panel="general">
					<div class="panel-head"><h3>Settings • General</h3></div>
					<div class="card">
						<div class="form-grid">
							<div class="field"><label>ROM Library Location</label><input id="setting-romLibraryLocation" type="text" /></div>
							<div class="field"><label>Save/State Storage Location</label><input id="setting-saveStateStorageLocation" type="text" /></div>
							<div class="field"><label>Auto-Save</label><select id="setting-autoSave"><option value="true">Enabled</option><option value="false">Disabled</option></select></div>
							<div class="field"><label>Auto-Load</label><select id="setting-autoLoad"><option value="true">Enabled</option><option value="false">Disabled</option></select></div>
							<div class="field"><label>Language</label><input id="setting-language" type="text" /></div>
							<div class="field"><label>Theme</label><select id="setting-theme"><option value="dark">Dark</option><option value="light">Light</option><option value="custom">Custom</option></select></div>
							<div class="field"><label>Update Channel</label><select id="setting-updateChannel"><option value="stable">Stable</option><option value="preview">Preview</option><option value="nightly">Nightly</option></select></div>
							<div class="field"><label>Video Renderer</label><select id="setting-renderer"><option value="OpenGL">OpenGL</option><option value="Vulkan">Vulkan</option><option value="DirectX">DirectX</option></select></div>
							<div class="field"><label>Internal Resolution</label><input id="setting-internalResolution" type="text" /></div>
							<div class="field"><label>Aspect Ratio</label><input id="setting-aspectRatio" type="text" /></div>
							<div class="field"><label>VSync</label><select id="setting-vsync"><option value="true">Enabled</option><option value="false">Disabled</option></select></div>
							<div class="field"><label>Audio Output Device</label><input id="setting-outputDevice" type="text" /></div>
							<div class="field"><label>Audio Volume</label><input id="setting-volume" type="number" min="0" max="100" /></div>
							<div class="field"><label>Audio Latency (ms)</label><input id="setting-latencyMs" type="number" min="10" max="500" /></div>
							<div class="field"><label>Audio Backend</label><input id="setting-backend" type="text" /></div>
							<div class="field"><label>Audio Sync</label><select id="setting-audioSync"><option value="true">Enabled</option><option value="false">Disabled</option></select></div>
							<div class="field"><label>Run Task Label</label><input id="setting-runTaskLabel" type="text" /></div>
							<div class="field"><label>Debug Task Label</label><input id="setting-debugTaskLabel" type="text" /></div>
						</div>
						<div class="actions">
							<button id="btn-save-settings">Save Settings</button>
							<button id="btn-reload-settings">Reload Settings</button>
							<button data-command="emulationUniverse.settings.openFile">Open Settings JSON</button>
						</div>
						<div class="status" id="settings-path-status">Settings file: loading...</div>
						<div class="status" id="automation-label-status">Automation labels: validating...</div>
					</div>
				</section>
				<section class="panel" data-panel="input-settings"><div class="panel-head"><h3>Settings • Input</h3></div><div class="grid"><div class="card"><h4>Players</h4><ul><li>Player device assignment</li><li>Hot-swap detection</li></ul></div><div class="card"><h4>Keyboard + Controller Keybinds</h4><ul><li>Per-console mapping</li><li>Emulator hotkeys</li><li>Analog calibration</li><li>Deadzone / Trigger range / Rumble</li></ul></div><div class="card"><h4>Controller Scan</h4><ul><li>Refresh devices</li><li>Device test panel</li></ul></div></div></section>
				<section class="panel" data-panel="audio"><div class="panel-head"><h3>Settings • Audio</h3></div><div class="card"><ul><li>Output device</li><li>Volume</li><li>Latency / Buffer size</li><li>Audio backend</li><li>Audio sync</li><li>Channel visualization</li></ul></div></section>
				<section class="panel" data-panel="video"><div class="panel-head"><h3>Settings • Video</h3></div><div class="card"><ul><li>Renderer (OpenGL/Vulkan/DirectX)</li><li>Internal resolution</li><li>Aspect ratio / VSync / Integer scaling</li><li>Shaders / Filters / Color correction</li><li>CRT simulation / Frame blending</li><li>Screenshot format</li></ul></div></section>
				<section class="panel" data-panel="network"><div class="panel-head"><h3>Settings • Network / Multiplayer</h3></div><div class="card"><ul><li>Netplay setup</li><li>Host / Join</li><li>Input delay settings</li><li>NAT traversal</li><li>Chat overlay</li></ul></div></section>
				<section class="panel" data-panel="storage"><div class="panel-head"><h3>Settings • Storage & Data</h3></div><div class="card"><ul><li>Save file directory</li><li>State file directory</li><li>BIOS directory</li><li>Memory card management</li><li>Backup / Restore configuration</li></ul></div></section>
				<section class="panel" data-panel="patches"><div class="panel-head"><h3>Settings • ROM Patch & Mods</h3></div><div class="card"><ul><li>IPS/UPS/BPS patch loader</li><li>Mod loader</li><li>Per-game patch management</li><li>Randomizer support</li></ul></div></section>
				<section class="panel" data-panel="advanced"><div class="panel-head"><h3>Settings • Advanced</h3></div><div class="card"><ul><li>CPU accuracy levels</li><li>Overclock / Underclock</li><li>Debug logging</li><li>Threading options</li><li>Region override</li><li>Timing adjustments</li></ul></div></section>
				<section class="panel" data-panel="theme"><div class="panel-head"><h3>Settings • UI / Theme</h3></div><div class="grid"><div class="card"><ul><li>Layout presets</li><li>Font size</li><li>Icon size</li><li>Custom themes</li><li>Accessibility options</li></ul></div><div class="card"><h4>Optional Professional Modules</h4><ul><li>Per-game settings overrides</li><li>Plugin manager</li><li>Benchmark mode</li><li>Frame timing graph</li><li>Audio latency test</li></ul></div></div></section>

				<div class="footer-note">Status: UI shell is live with command wiring, menu navigation, and persisted settings integration in VS Code.</div>
			</main>
		</div>
		<div id="command-toast" class="toast" role="status" aria-live="polite"></div>
	</div>

	<script nonce="${nonce}">
		(() => {
			const vscode = acquireVsCodeApi();
			const tabs = Array.from(document.querySelectorAll('.tab'));
			const menuItems = Array.from(document.querySelectorAll('.menu-item[data-tab-target]'));
			const panels = Array.from(document.querySelectorAll('.panel'));
			const commandButtons = Array.from(document.querySelectorAll('[data-command]'));
			const romRequiredButtons = Array.from(document.querySelectorAll('[data-requires-rom="true"]'));
			const debugRequiredButtons = Array.from(document.querySelectorAll('[data-requires-debug="true"]'));
			const commandToast = document.getElementById('command-toast');

			const form = {
				romLibraryLocation: document.getElementById('setting-romLibraryLocation'),
				saveStateStorageLocation: document.getElementById('setting-saveStateStorageLocation'),
				autoSave: document.getElementById('setting-autoSave'),
				autoLoad: document.getElementById('setting-autoLoad'),
				language: document.getElementById('setting-language'),
				theme: document.getElementById('setting-theme'),
				updateChannel: document.getElementById('setting-updateChannel'),
				renderer: document.getElementById('setting-renderer'),
				internalResolution: document.getElementById('setting-internalResolution'),
				aspectRatio: document.getElementById('setting-aspectRatio'),
				vsync: document.getElementById('setting-vsync'),
				outputDevice: document.getElementById('setting-outputDevice'),
				volume: document.getElementById('setting-volume'),
				latencyMs: document.getElementById('setting-latencyMs'),
				backend: document.getElementById('setting-backend'),
				audioSync: document.getElementById('setting-audioSync'),
				runTaskLabel: document.getElementById('setting-runTaskLabel'),
				debugTaskLabel: document.getElementById('setting-debugTaskLabel')
			};

			const statusNode = document.getElementById('settings-path-status');
			const automationStatusNode = document.getElementById('automation-label-status');
			const saveButton = document.getElementById('btn-save-settings');
			const reloadButton = document.getElementById('btn-reload-settings');
			const openSettingsButton = document.getElementById('btn-open-settings');
			const romStateChip = document.getElementById('rom-state-chip');
			const debugStateChip = document.getElementById('debug-state-chip');
			let toastTimer = undefined;
			let romLoaded = false;
			let debugActive = false;

			function syncAvailability() {
				for (const button of romRequiredButtons) {
					button.disabled = !romLoaded;
				}
				for (const button of debugRequiredButtons) {
					button.disabled = !debugActive;
				}
			}

			function setRomLoadedState(isLoaded, romPath) {
				romLoaded = Boolean(isLoaded);
				if (!romLoaded) {
					debugActive = false;
				}
				syncAvailability();

				if (romStateChip) {
					romStateChip.classList.remove('idle', 'good', 'warn');
					if (romLoaded) {
						romStateChip.classList.add('good');
						const shortName = (romPath || '').split(/[\\/]/).pop() || 'Loaded';
						romStateChip.textContent = 'ROM: ' + shortName;
					} else {
						romStateChip.classList.add('idle');
						romStateChip.textContent = 'ROM: Not Loaded';
					}
				}
			}

			function setDebugActiveState(isActive) {
				debugActive = Boolean(isActive) && romLoaded;
				syncAvailability();

				if (debugStateChip) {
					debugStateChip.classList.remove('idle', 'good', 'warn');
					if (debugActive) {
						debugStateChip.classList.add('warn');
						debugStateChip.textContent = 'Debug: Active';
					} else {
						debugStateChip.classList.add('idle');
						debugStateChip.textContent = 'Debug: Inactive';
					}
				}
			}

			function toTopMenu(panelId) {
				const map = {
					file: 'file',
					emulation: 'emulation',
					input: 'input',
					view: 'view',
					debug: 'debug',
					help: 'help',
					settingsShell: 'general',
					general: 'general',
					inputSettings: 'general',
					audio: 'general',
					video: 'general',
					network: 'general',
					storage: 'general',
					patches: 'general',
					advanced: 'general',
					theme: 'general'
				};

				const normalized = String(panelId || '').replace(/-([a-z])/g, (_, c) => c.toUpperCase());
				return map[normalized] || 'general';
			}

			function showToast(message, level) {
				if (!commandToast) return;
				commandToast.textContent = message;
				commandToast.classList.remove('success', 'error', 'show');
				commandToast.classList.add(level === 'error' ? 'error' : 'success');
				commandToast.classList.add('show');

				if (toastTimer) {
					clearTimeout(toastTimer);
				}
				toastTimer = setTimeout(() => {
					if (commandToast) {
						commandToast.classList.remove('show');
					}
				}, 2400);
			}

			function activate(id) {
				for (const tab of tabs) {
					tab.classList.toggle('active', tab.dataset.tab === id);
				}
				for (const panel of panels) {
					panel.classList.toggle('active', panel.dataset.panel === id);
				}
				const activeTopMenu = toTopMenu(id);
				for (const menuItem of menuItems) {
					menuItem.classList.toggle('active', menuItem.dataset.tabTarget === activeTopMenu);
				}
			}

			for (const tab of tabs) {
				tab.addEventListener('click', () => activate(tab.dataset.tab));
			}

			for (const item of menuItems) {
				item.addEventListener('click', () => {
					const target = item.dataset.tabTarget;
					if (!target) return;
					activate(target);
				});
			}

			for (const btn of commandButtons) {
				btn.addEventListener('click', () => {
					if (btn.disabled) {
						if (btn.dataset.requiresDebug === 'true' && !debugActive) {
							showToast('Start a debug session first.', 'error');
							return;
						}
						showToast('Load a ROM first to use this action.', 'error');
						return;
					}
					const command = btn.dataset.command;
					if (!command) return;
					vscode.postMessage({ type: 'executeCommand', command });
				});
			}

			if (openSettingsButton) {
				openSettingsButton.addEventListener('click', () => activate('general'));
			}

			function readSettingsFromForm() {
				return {
					general: {
						romLibraryLocation: form.romLibraryLocation.value,
						saveStateStorageLocation: form.saveStateStorageLocation.value,
						autoSave: form.autoSave.value === 'true',
						autoLoad: form.autoLoad.value === 'true',
						language: form.language.value,
						theme: form.theme.value,
						updateChannel: form.updateChannel.value
					},
					video: {
						renderer: form.renderer.value,
						internalResolution: form.internalResolution.value,
						aspectRatio: form.aspectRatio.value,
						vsync: form.vsync.value === 'true'
					},
					audio: {
						outputDevice: form.outputDevice.value,
						volume: Number(form.volume.value || 100),
						latencyMs: Number(form.latencyMs.value || 60),
						backend: form.backend.value,
						audioSync: form.audioSync.value === 'true'
					},
					automation: {
						runTaskLabel: form.runTaskLabel.value,
						debugTaskLabel: form.debugTaskLabel.value
					}
				};
			}

			function writeSettingsToForm(settings) {
				form.romLibraryLocation.value = settings.general.romLibraryLocation;
				form.saveStateStorageLocation.value = settings.general.saveStateStorageLocation;
				form.autoSave.value = String(settings.general.autoSave);
				form.autoLoad.value = String(settings.general.autoLoad);
				form.language.value = settings.general.language;
				form.theme.value = settings.general.theme;
				form.updateChannel.value = settings.general.updateChannel;
				form.renderer.value = settings.video.renderer;
				form.internalResolution.value = settings.video.internalResolution;
				form.aspectRatio.value = settings.video.aspectRatio;
				form.vsync.value = String(settings.video.vsync);
				form.outputDevice.value = settings.audio.outputDevice;
				form.volume.value = String(settings.audio.volume);
				form.latencyMs.value = String(settings.audio.latencyMs);
				form.backend.value = settings.audio.backend;
				form.audioSync.value = String(settings.audio.audioSync);
				form.runTaskLabel.value = settings.automation.runTaskLabel;
				form.debugTaskLabel.value = settings.automation.debugTaskLabel;
			}

			if (saveButton) {
				saveButton.addEventListener('click', () => {
					vscode.postMessage({ type: 'saveSettings', settings: readSettingsFromForm() });
				});
			}

			if (reloadButton) {
				reloadButton.addEventListener('click', () => {
					vscode.postMessage({ type: 'loadSettings' });
				});
			}

			window.addEventListener('message', event => {
				const msg = event.data;
				if (msg?.type === 'settingsLoaded' && msg.settings) {
					writeSettingsToForm(msg.settings);
					if (statusNode) {
						statusNode.textContent = 'Settings file: ' + msg.settingsPath;
					}
				}
				if (msg?.type === 'settingsSaved' && statusNode) {
					statusNode.textContent = (statusNode.textContent || 'Settings file:') + ' (saved)';
					showToast('Settings saved.', 'success');
				}
				if (msg?.type === 'actionStatus' && msg.text) {
					showToast(msg.text, msg.level === 'error' ? 'error' : 'success');
				}
				if (msg?.type === 'romState') {
					setRomLoadedState(msg.loaded, msg.romPath || '');
				}
				if (msg?.type === 'debugState') {
					setDebugActiveState(msg.active);
				}
				if (msg?.type === 'automationValidation' && automationStatusNode) {
					if (Array.isArray(msg.missing) && msg.missing.length > 0) {
						automationStatusNode.textContent = 'Automation labels: missing tasks -> ' + msg.missing.join(', ');
					} else {
						automationStatusNode.textContent = 'Automation labels: all task labels are valid.';
					}
				}
			});

			setRomLoadedState(false, '');
			setDebugActiveState(false);
			vscode.postMessage({ type: 'ready' });
		})();
	</script>
</body>
</html>`;
}

function getNonce(): string {
	const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
	let result = '';
	for (let i = 0; i < 32; i++) {
		result += chars.charAt(Math.floor(Math.random() * chars.length));
	}
	return result;
}