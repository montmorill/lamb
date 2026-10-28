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
