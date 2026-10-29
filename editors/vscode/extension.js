// Lean-style Unicode input for Lamb.
//
// Type a backslash followed by an abbreviation and press Tab to replace it:
// `\forall` + Tab -> `∀`, `\to` + Tab -> `→`, `\lam` + Tab -> `λ`.
//
// Unlike Lean, abbreviations are not replaced eagerly while typing: `\` is the
// Lamb lambda operator, so `\x.` must survive untouched. Replacement only
// happens on the explicit Tab (or via the Insert Unicode Symbol command).

const vscode = require('vscode');
const fs = require('fs');
const path = require('path');
const builtinTranslations = require('./abbreviations.json');

const CONTEXT_KEY = 'lamb.input.isActive';
const ABBREVIATION_CHAR = /[A-Za-z0-9_=<>!+\-*/^|]/;
const NAME_CHAR = /[\p{L}\p{N}_?'/-]/u;
const DEFINITION_NAME = /^(?!(?:open|in|section)(?![\p{L}\p{N}_?'/-]))(?![\p{Nd}/])[\p{L}\p{N}_?'/-]+/u;

// [name, signature, description]. The signatures must match the ones the
// interpreter prints in `:help`; argument completion is derived from them.
// Keep this list in sync with COMMANDS in lamb.c.
const COMMAND_COMPLETIONS = [
    ['ast', '<expr>', 'print the AST of the expression'],
    ['debug', '<expr>', 'Step debug the evaluation of an expression'],
    ['delete', '<name>', 'delete a binding by name'],
    ['edit', '[path]', 'Edit current active file. Reload it on exit.'],
    ['help', '', 'print this help message'],
    ['list', '[names...]', 'list the bindings'],
    ['load', '[path]', 'Load/reload bindings from a file.'],
    ['quit', '', 'quit the REPL'],
    ['save', '[path]', 'Save current bindings to a file.'],
    ['set', '<key> <value>', 'Set a setting: `debruijn` controls variable output, `unicode` prints λ, `shorthand` elides chained lambdas.'],
];

// [key, detail]. The values a key accepts are completed from its own table.
const SETTINGS = [
    ['debruijn', 'Variable output mode'],
    ['unicode', 'Lambda output symbol'],
    ['shorthand', 'Elide chained lambda symbols'],
];

const DEBRUIJN_MODES = [
    ['off', 'Print bound variables by the name of their binder'],
    ['on', 'Print bound variables as indices, keeping binder names'],
    ['full', 'Print anonymous binders and every variable as an index'],
];

const UNICODE_MODES = [
    ['off', 'Print binders with \\'],
    ['on', 'Print binders with λ'],
];

const SHORTHAND_MODES = [
    ['off', 'Print every binder with its own lambda symbol'],
    ['on', 'Print chained named binders as \\x.y.body'],
];

const SETTING_MODES = new Map([
    ['debruijn', DEBRUIJN_MODES],
    ['unicode', UNICODE_MODES],
    ['shorthand', SHORTHAND_MODES],
]);

const COMMANDS_BY_NAME = new Map(COMMAND_COMPLETIONS.map(command => [command[0], command]));

const MAGIC_COMPLETIONS = [
    ['trace', 'Print the reduced argument and return it'],
    ['void', 'Force the argument and return #void'],
];

/** @type {vscode.TextEditorDecorationType | undefined} */
let underlineDecoration;

/** @type {Record<string, string>} */
let translations = {};

/** @type {vscode.TextEditor | undefined} */
let decoratedEditor;

function config() {
    return vscode.workspace.getConfiguration('lamb.input');
}

function isEnabled() {
    return config().get('enabled', true);
}

function leader() {
    return config().get('leader', '\\') || '\\';
}

function rebuildTranslations() {
    translations = Object.assign(
        {},
        builtinTranslations,
        config().get('customTranslations', {}) || {},
    );
}

/**
 * Returns the abbreviation immediately before `position`, or undefined.
 * @param {vscode.TextDocument} document
 * @param {vscode.Position} position
 */
function candidateAt(document, position) {
    const line = document.lineAt(position.line).text.slice(0, position.character);
    let start = line.length;
    while (start > 0 && ABBREVIATION_CHAR.test(line[start - 1])) {
        start--;
    }

    const prefix = leader();
    const leaderStart = start - prefix.length;
    if (leaderStart < 0 || line.slice(leaderStart, start) !== prefix || start === line.length) {
        return undefined;
    }

    return {
        abbreviation: line.slice(start),
        range: new vscode.Range(position.line, leaderStart, position.line, position.character),
    };
}

/**
 * Exact matches win; otherwise the shortest abbreviation starting with the
 * typed prefix is used (the same rule Lean's Tab conversion applies).
 *
 * Prefixes shorter than two characters are ignored: `\f` and `\x` are common
 * lambda parameters, and converting them on Tab would be surprising even
 * though they happen to prefix `forall` and `xi`.
 * @param {string} abbreviation
 */
function findTranslation(abbreviation) {
    if (Object.prototype.hasOwnProperty.call(translations, abbreviation)) {
        return { abbreviation, symbol: translations[abbreviation] };
    }
    if (abbreviation.length < 2) {
        return undefined;
    }

    const matches = Object.keys(translations)
        .filter(key => key.length > abbreviation.length && key.startsWith(abbreviation))
        .sort((a, b) => a.length - b.length || a.localeCompare(b));

    if (matches.length === 0) {
        return undefined;
    }
    return { abbreviation: matches[0], symbol: translations[matches[0]] };
}

/**
 * @param {vscode.TextDocument} document
 * @param {vscode.Position} position
 */
function stateAt(document, position) {
    if (!isEnabled() || document.languageId !== 'lamb') {
        return undefined;
    }
    const candidate = candidateAt(document, position);
    if (!candidate) {
        return undefined;
    }
    const translation = findTranslation(candidate.abbreviation);
    if (!translation) {
        return undefined;
    }
    return { candidate, translation };
}

/**
 * Updates the underline decoration and the context key that arms the Tab
 * keybinding.
 * @param {vscode.TextEditor | undefined} editor
 */
async function updateState(editor) {
    if (decoratedEditor && decoratedEditor !== editor) {
        decoratedEditor.setDecorations(underlineDecoration, []);
    }
    decoratedEditor = editor;

    let state;
    if (editor && editor.selection.isEmpty) {
        state = stateAt(editor.document, editor.selection.active);
    }

    if (editor) {
        editor.setDecorations(underlineDecoration, state ? [state.candidate.range] : []);
    }

    await vscode.commands.executeCommand('setContext', CONTEXT_KEY, Boolean(state));
}

/**
 * @param {vscode.TextEditor} editor
 */
async function convert(editor) {
    if (!editor) {
        return;
    }

    const conversions = [];
    for (const selection of editor.selections) {
        if (!selection.isEmpty) {
            continue;
        }
        const state = stateAt(editor.document, selection.active);
        if (state) {
            conversions.push(state);
        }
    }
    if (conversions.length === 0) {
        return;
    }

    const applied = await editor.edit(builder => {
        for (const { candidate, translation } of conversions) {
            builder.replace(candidate.range, translation.symbol);
        }
    });
    if (!applied) {
        return;
    }

    // VS Code keeps multi-cursor selections in sync with edits; for the common
    // single-cursor case put the caret right after the inserted symbol.
    if (editor.selections.length === 1) {
        const { candidate, translation } = conversions[0];
        const cursor = candidate.range.start.translate(0, translation.symbol.length);
        editor.selection = new vscode.Selection(cursor, cursor);
    }
    await updateState(editor);
}

/** @type {Map<string, { mtimeMs: number, names: string[] }>} */
const fileDefinitionCache = new Map();

/**
 * Extracts the binding names defined in a Lamb source file.
 * @param {string} text
 */
function definitionsInText(text) {
    const names = [];
    for (const rawLine of text.split(/\r?\n/)) {
        const line = rawLine.split('//')[0];
        for (const statement of line.split(';')) {
            // Binding definitions use `name = body`.
            const assign = statement.indexOf('=');
            if (assign < 0) {
                continue;
            }
            const match = DEFINITION_NAME.exec(statement.slice(0, assign).trim());
            if (match) {
                names.push(match[0]);
            }
        }
    }
    return names;
}

/**
 * @param {string} file
 */
function definitionsInFile(file) {
    try {
        const mtimeMs = fs.statSync(file).mtimeMs;
        const cached = fileDefinitionCache.get(file);
        if (cached && cached.mtimeMs === mtimeMs) {
            return cached.names;
        }
        const names = definitionsInText(fs.readFileSync(file, 'utf8'));
        fileDefinitionCache.set(file, { mtimeMs, names });
        return names;
    } catch {
        return [];
    }
}

/**
 * Looks for std.lamb next to the document, then in each workspace folder.
 * @param {vscode.TextDocument} document
 */
function stdLambPath(document) {
    const candidates = [];
    if (document.uri.scheme === 'file') {
        candidates.push(path.join(path.dirname(document.uri.fsPath), 'std.lamb'));
    }
    for (const folder of vscode.workspace.workspaceFolders || []) {
        candidates.push(path.join(folder.uri.fsPath, 'std.lamb'));
    }
    return candidates.find(file => {
        try {
            return fs.statSync(file).isFile();
        } catch {
            return false;
        }
    });
}

/**
 * Returns the completion range start and the text typed since it. A leading
 * `:` or `#` is part of the range so command and magic completions replace it.
 * @param {string} line
 * @param {number} character
 */
function completionWord(line, character) {
    let start = character;
    while (start > 0 && NAME_CHAR.test(line[start - 1])) {
        start -= 1;
    }
    if (start > 0 && (line[start - 1] === ':' || line[start - 1] === '#')) {
        start -= 1;
    }
    return { start, prefix: line.slice(start, character) };
}

/**
 * Binding completions for the current document and std.lamb.
 * @param {vscode.TextDocument} document
 * @param {vscode.Range} range
 */
function bindingCompletionItems(document, range) {
    const items = [];
    const seen = new Set();
    const add = (name, detail) => {
        if (seen.has(name)) {
            return;
        }
        seen.add(name);
        const item = new vscode.CompletionItem(name, vscode.CompletionItemKind.Function);
        item.range = range;
        item.detail = detail;
        items.push(item);
    };

    for (const name of definitionsInText(document.getText())) {
        add(name, 'binding in this file');
    }
    const std = stdLambPath(document);
    if (std) {
        for (const name of definitionsInFile(std)) {
            add(name, 'binding from std.lamb');
        }
    }

    return items;
}

/**
 * Derives what a command's argument means from its `:help` signature. Returns
 * undefined when the signature gives no meaningful completion, such as a file
 * path or no argument at all.
 * @param {string} signature
 */
function argumentKind(signature) {
    const argument = signature.trim();
    if (argument === '[on|off|full]') {
        return 'mode';
    }
    if (argument === '<key> <value>') {
        return 'set';
    }
    if (argument === '<name>') {
        return 'name';
    }
    if (argument === '[names...]') {
        return 'names';
    }
    if (argument === '<expr>') {
        return 'expr';
    }
    return undefined;
}

/**
 * Detects a `:command ` prefix before the cursor and describes the argument
 * being typed. Returns undefined when the cursor is not in command arguments.
 * @param {string} line
 * @param {number} character
 */
function commandArgumentAt(line, character) {
    const match = /^\s*:([\p{L}\p{N}_?'-]+)\s+/u.exec(line.slice(0, character));
    if (!match) {
        return undefined;
    }

    // An unknown command still means the typed word is an argument, not a
    // binding, so do not fall back to expression completion.
    const command = COMMANDS_BY_NAME.get(match[1]);
    const kind = command ? argumentKind(command[1]) : undefined;
    if (!kind) {
        return { kind: undefined };
    }

    // Everything after `:command ` split into completed arguments and the word
    // currently under the cursor. The last word is always the current one.
    const rest = line.slice(match[0].length, character);
    const words = rest.length === 0 ? [''] : rest.split(/\s+/u);
    return { kind, completed: words.length - 1, args: words.slice(0, -1) };
}

/**
 * @param {vscode.Range} range
 * @param {string[][]} modes
 */
function modeCompletionItems(range, modes) {
    return modes.map(([name, description]) => {
        const item = new vscode.CompletionItem(name, vscode.CompletionItemKind.Keyword);
        item.range = range;
        item.detail = description;
        item.sortText = '0' + name;
        return item;
    });
}

/**
 * @param {vscode.Range} range
 */
function settingCompletionItems(range) {
    return SETTINGS.map(([name, description]) => {
        const item = new vscode.CompletionItem(name, vscode.CompletionItemKind.Keyword);
        item.range = range;
        item.detail = description;
        item.sortText = '0' + name;
        return item;
    });
}

/**
 * @param {vscode.TextDocument} document
 * @param {vscode.Position} position
 */
function provideCompletionItems(document, position) {
    const line = document.lineAt(position.line).text;
    if (line.slice(0, position.character).includes('//')) {
        return undefined;
    }

    const argument = commandArgumentAt(line, position.character);
    if (argument) {
        const { start, prefix } = completionWord(line, position.character);
        const range = new vscode.Range(position.line, start, position.line, position.character);

        if (argument.kind === undefined) {
            // File paths and commands without arguments have nothing to suggest.
            return undefined;
        }
        if (argument.kind === 'mode') {
            if (argument.completed > 0) {
                return undefined;
            }
            return modeCompletionItems(range, DEBRUIJN_MODES);
        }
        if (argument.kind === 'set') {
            if (argument.completed === 0) {
                return settingCompletionItems(range);
            }
            const modes = argument.completed === 1
                ? SETTING_MODES.get(argument.args[0])
                : undefined;
            return modes ? modeCompletionItems(range, modes) : undefined;
        }
        if (argument.kind === 'name' && argument.completed > 0) {
            return undefined;
        }

        const items = bindingCompletionItems(document, range);
        if (argument.kind === 'expr' && prefix.startsWith('#')) {
            for (const [name, description] of MAGIC_COMPLETIONS) {
                const item = new vscode.CompletionItem('#' + name, vscode.CompletionItemKind.Function);
                item.range = range;
                item.detail = description;
                items.push(item);
            }
        }
        return items.length > 0 ? items : undefined;
    }

    const { start, prefix } = completionWord(line, position.character);
    // Require at least one typed character, so plain Tab still indents.
    if (prefix.length === 0) {
        return undefined;
    }

    const range = new vscode.Range(position.line, start, position.line, position.character);

    if (prefix.startsWith(':')) {
        const items = [];
        for (const [name, signature, description] of COMMAND_COMPLETIONS) {
            const item = new vscode.CompletionItem(':' + name, vscode.CompletionItemKind.Keyword);
            item.range = range;
            item.detail = signature ? `:${name} ${signature}` : `:${name}`;
            item.documentation = description;
            item.sortText = '0' + name;
            items.push(item);
        }
        return items;
    }

    if (prefix.startsWith('#')) {
        const items = [];
        for (const [name, description] of MAGIC_COMPLETIONS) {
            const item = new vscode.CompletionItem('#' + name, vscode.CompletionItemKind.Function);
            item.range = range;
            item.detail = description;
            item.sortText = '0' + name;
            items.push(item);
        }
        return items;
    }

    const items = bindingCompletionItems(document, range);
    return items.length > 0 ? items : undefined;
}

/**
 * @param {vscode.TextEditor | undefined} editor
 */
async function insertSymbol(editor) {
    if (!editor || editor.document.languageId !== 'lamb') {
        vscode.window.showInformationMessage('Open a Lamb file to insert a Unicode symbol.');
        return;
    }

    const items = Object.keys(translations)
        .sort()
        .map(key => ({
            label: translations[key],
            description: '\\' + key,
            key,
        }));

    const picked = await vscode.window.showQuickPick(items, {
        placeHolder: 'Insert a Unicode symbol (type an abbreviation such as "forall" to filter)',
        matchOnDescription: true,
    });
    if (!picked) {
        return;
    }

    await editor.edit(builder => {
        for (const selection of editor.selections) {
            builder.replace(selection, picked.label);
        }
    });
}

function activate(context) {
    rebuildTranslations();

    underlineDecoration = vscode.window.createTextEditorDecorationType({
        textDecoration: 'underline',
    });

    context.subscriptions.push(
        underlineDecoration,
        vscode.commands.registerTextEditorCommand('lamb.input.convert', editor => convert(editor)),
        vscode.commands.registerTextEditorCommand('lamb.input.insertSymbol', editor => insertSymbol(editor)),
        vscode.languages.registerCompletionItemProvider(
            'lamb',
            { provideCompletionItems },
            '#',
        ),
        vscode.window.onDidChangeActiveTextEditor(editor => updateState(editor)),
        vscode.window.onDidChangeTextEditorSelection(event => updateState(event.textEditor)),
        vscode.workspace.onDidChangeTextDocument(event => {
            const editor = vscode.window.activeTextEditor;
            if (editor && event.document === editor.document) {
                updateState(editor);
            }
        }),
        vscode.workspace.onDidChangeConfiguration(event => {
            if (!event.affectsConfiguration('lamb.input')) {
                return;
            }
            rebuildTranslations();
            updateState(vscode.window.activeTextEditor);
        }),
    );

    updateState(vscode.window.activeTextEditor);
}

function deactivate() {
    decoratedEditor = undefined;
}

module.exports = { activate, deactivate };
