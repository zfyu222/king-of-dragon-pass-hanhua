const crypto = require("crypto");
const fs = require("fs");
const path = require("path");

const projectDir = path.resolve(__dirname, "..");
const gameDir = path.join(projectDir, "Game");
const sourceCsv = path.join(projectDir, "king-of-dragon-pass_text.csv");
const translatedCsv = path.join(
  projectDir,
  "king-of-dragon-pass_text_translated.csv",
);
const catalogTsv = path.join(
  projectDir,
  "king-of-dragon-pass_text_catalog.tsv",
);
const outputRoot = path.join(projectDir, "release", "KoDPCNHook", "overlay");
const outputScenes = path.join(outputRoot, "KoDp", "scenes.dat");
const outputManifest = path.join(outputRoot, "manifest.json");
const outputTranslations = path.join(
  projectDir,
  "release",
  "KoDPCNHook",
  "translations.tsv",
);

function readSingleColumnCsv(filePath) {
  let text = fs.readFileSync(filePath, "utf8");
  if (text.charCodeAt(0) === 0xfeff) text = text.slice(1);

  const rows = [];
  let field = "";
  let inQuotes = false;
  let quoted = false;

  for (let i = 0; i < text.length; i += 1) {
    const ch = text[i];
    if (inQuotes) {
      if (ch === '"' && text[i + 1] === '"') {
        field += '"';
        i += 1;
      } else if (ch === '"') {
        inQuotes = false;
      } else {
        field += ch;
      }
      continue;
    }

    if (ch === '"' && field.length === 0) {
      inQuotes = true;
      quoted = true;
    } else if (ch === ",") {
      throw new Error(`${filePath}: detected more than one CSV column`);
    } else if (ch === "\r" || ch === "\n") {
      if (ch === "\r" && text[i + 1] === "\n") i += 1;
      rows.push(field);
      field = "";
      quoted = false;
    } else if (quoted && ch !== " " && ch !== "\t") {
      throw new Error(`${filePath}: unexpected data after closing quote`);
    } else if (!quoted) {
      field += ch;
    }
  }

  if (inQuotes) throw new Error(`${filePath}: unterminated CSV quote`);
  if (field.length > 0 || !/[\r\n]$/.test(text)) rows.push(field);
  return rows;
}

function unescapeCorpusText(text) {
  let result = "";
  for (let i = 0; i < text.length; i += 1) {
    if (text[i] !== "\\") {
      result += text[i];
      continue;
    }
    const next = text[i + 1];
    if (next === "\\") {
      result += "\\";
      i += 1;
    } else if (next === "n") {
      result += "\n";
      i += 1;
    } else if (next === "r") {
      result += "\r";
      i += 1;
    } else if (next === "t") {
      result += "\t";
      i += 1;
    } else if (
      next === "x" &&
      /^[0-9A-Fa-f]{2}$/.test(text.slice(i + 2, i + 4))
    ) {
      result += String.fromCharCode(Number.parseInt(text.slice(i + 2, i + 4), 16));
      i += 3;
    } else {
      result += "\\";
    }
  }
  return result;
}

function escapeCorpusText(value) {
  return value
    .replaceAll("\\", "\\\\")
    .replaceAll("\r", "\\r")
    .replaceAll("\n", "\\n")
    .replaceAll("\t", "\\t")
    .replace(/[\x00-\x08\x0B\x0C\x0E-\x1F\x7F]/g, (ch) =>
      `\\x${ch.charCodeAt(0).toString(16).toUpperCase().padStart(2, "0")}`,
    );
}

function sha256(data) {
  return crypto.createHash("sha256").update(data).digest("hex").toUpperCase();
}

function readCatalog(sourceRows) {
  const lines = fs
    .readFileSync(catalogTsv, "utf8")
    .replace(/^\uFEFF/, "")
    .split(/\r?\n/);
  const records = [];
  for (const line of lines.slice(1)) {
    if (!line) continue;
    const [indexText, source, locator, kind] = line.split("\t");
    if (kind !== "scenes.dat") continue;
    const match = /^scene=(\d+);offset=0x([0-9A-F]+);bytes=(\d+)$/i.exec(
      locator,
    );
    if (!match) throw new Error(`Unexpected scenes.dat locator: ${locator}`);
    const corpusIndex = Number(indexText);
    if (!Number.isInteger(corpusIndex) || sourceRows[corpusIndex] === undefined) {
      throw new Error(`Invalid corpus index: ${indexText}`);
    }
    records.push({
      corpusIndex,
      source,
      scene: Number(match[1]),
      offset: Number.parseInt(match[2], 16),
      byteLength: Number(match[3]),
    });
  }
  return records;
}

function readAllCatalog() {
  const lines = fs
    .readFileSync(catalogTsv, "utf8")
    .replace(/^\uFEFF/, "")
    .split(/\r?\n/);
  const records = [];
  for (const line of lines.slice(1)) {
    if (!line) continue;
    const [indexText, source, locator, kind] = line.split("\t");
    records.push({
      corpusIndex: Number(indexText),
      source,
      locator,
      kind,
    });
  }
  return records;
}

function xmlEscape(value) {
  return value
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&apos;");
}

function writeOverlay(relative, content) {
  const outputPath = path.join(outputRoot, ...relative.split("/"));
  fs.mkdirSync(path.dirname(outputPath), { recursive: true });
  fs.writeFileSync(outputPath, content, "utf8");
}

function buildTextFileOverlays(translatedRows) {
  const supported = new Set([
    "UI caption",
    "plist string",
    "localization XML",
    "help text",
    "HTML text",
  ]);
  const byFile = new Map();
  for (const record of readAllCatalog()) {
    if (!supported.has(record.kind)) continue;
    const records = byFile.get(record.source) || [];
    records.push(record);
    byFile.set(record.source, records);
  }

  let generatedFiles = 0;
  let replacedRecords = 0;
  for (const [relative, records] of byFile) {
    const replacements = new Map(
      records.map((record) => [
        record.locator,
        unescapeCorpusText(translatedRows[record.corpusIndex]),
      ]),
    );
    let content = fs.readFileSync(
      path.join(gameDir, ...relative.split("/")),
      "utf8",
    );

    if (records[0].kind === "UI caption") {
      let ordinal = 0;
      content = content.replace(
        /^(\s*(?!\/\/)\s*caption\s+")((?:\\.|[^"])*)(")/gm,
        (whole, prefix, value, suffix) => {
          ordinal += 1;
          const translated = replacements.get(`caption=${ordinal}`);
          if (translated === undefined) return whole;
          replacedRecords += 1;
          const escaped = translated
            .replaceAll("\\", "\\\\")
            .replaceAll("\r", "")
            .replaceAll("\n", "\\n")
            .replaceAll('"', '\\"');
          return `${prefix}${escaped}${suffix}`;
        },
      );
    } else if (records[0].kind === "plist string") {
      let ordinal = 0;
      content = content.replace(
        /(<string>)([\s\S]*?)(<\/string>)/gi,
        (whole, prefix, value, suffix) => {
          ordinal += 1;
          const translated = replacements.get(`string=${ordinal}`);
          if (translated === undefined) return whole;
          replacedRecords += 1;
          return `${prefix}${xmlEscape(translated)}${suffix}`;
        },
      );
    } else if (records[0].kind === "localization XML") {
      let ordinal = 0;
      content = content.replace(
        /(<LocalizeString>)([\s\S]*?)(<\/LocalizeString>)/i,
        (whole, rootPrefix, body, rootSuffix) => {
          const translatedBody = body.replace(
            /(<([A-Za-z][\w.-]*)>)([\s\S]*?)(<\/\2>)/g,
            (innerWhole, prefix, tag, value, suffix) => {
              ordinal += 1;
              const translated = replacements.get(`${tag}=${ordinal}`);
              if (translated === undefined) return innerWhole;
              replacedRecords += 1;
              return `${prefix}${xmlEscape(translated)}${suffix}`;
            },
          );
          return `${rootPrefix}${translatedBody}${rootSuffix}`;
        },
      );
    } else if (records[0].kind === "help text") {
      const newline = content.includes("\r\n") ? "\r\n" : "\n";
      const paragraphs = content.replace(/\r\n?/g, "\n").split(/\n\s*\n/);
      content = paragraphs
        .map((paragraph, index) => {
          const translated = replacements.get(`paragraph=${index + 1}`);
          if (translated === undefined) return paragraph;
          replacedRecords += 1;
          return translated;
        })
        .join(`${newline}${newline}`);
    } else if (records[0].kind === "HTML text") {
      let ordinal = 0;
      content = content.replace(
        /(<(title|h[1-6]|p|li|td|th)\b[^>]*>)([\s\S]*?)(<\/\2>)/gi,
        (whole, prefix, tag, value, suffix) => {
          ordinal += 1;
          const translated = replacements.get(
            `${tag.toLowerCase()}=${ordinal}`,
          );
          if (translated === undefined) return whole;
          replacedRecords += 1;
          return `${prefix}${xmlEscape(translated)}${suffix}`;
        },
      );
    }
    writeOverlay(relative, content);
    generatedFiles += 1;
  }
  return { generatedFiles, replacedRecords };
}

function encodeStringRecord(text) {
  const payload = Buffer.from(unescapeCorpusText(text), "utf8");
  if (payload.length > 0xffff) {
    throw new Error(`A translated string is too large: ${payload.length} bytes`);
  }
  const padding = payload.length % 2 === 0 ? 1 : 0;
  const record = Buffer.alloc(2 + payload.length + 1 + padding);
  record.writeUInt16LE(payload.length, 0);
  payload.copy(record, 2);
  return record;
}

function main() {
  const sourceRows = readSingleColumnCsv(sourceCsv);
  const translatedRows = readSingleColumnCsv(translatedCsv);
  if (sourceRows.length !== translatedRows.length) {
    throw new Error(
      `CSV row count mismatch: ${sourceRows.length} vs ${translatedRows.length}`,
    );
  }

  const inputPath = path.join(gameDir, "KoDp", "scenes.dat");
  const input = fs.readFileSync(inputPath);
  const sceneCount = input.readUInt16LE(4);
  const pointerTableEnd = input.readUInt16LE(6);
  if (pointerTableEnd !== 8 + sceneCount * 4) {
    throw new Error("Unexpected scenes.dat pointer table");
  }

  const oldOffsets = Array.from({ length: sceneCount }, (_, index) =>
    input.readUInt32LE(8 + index * 4),
  );
  const records = readCatalog(sourceRows);
  const byScene = new Map();
  for (const record of records) {
    const list = byScene.get(record.scene) || [];
    list.push(record);
    byScene.set(record.scene, list);
  }

  const prefix = Buffer.from(input.subarray(0, oldOffsets[0]));
  const outputBlocks = [];
  let outputOffset = prefix.length;
  let replacedOccurrences = 0;

  for (let scene = 1; scene <= sceneCount; scene += 1) {
    const oldStart = oldOffsets[scene - 1];
    const oldWordCount = input.readUInt16LE(oldStart);
    const oldEnd = oldStart + 2 + oldWordCount * 2;
    const expectedEnd =
      scene < sceneCount ? oldOffsets[scene] : input.length;
    if (oldEnd !== expectedEnd) {
      throw new Error(`Scene ${scene} has an unexpected gap or overlap`);
    }

    prefix.writeUInt32LE(outputOffset, 8 + (scene - 1) * 4);
    const pieces = [];
    let cursor = oldStart + 2;
    const sceneRecords = (byScene.get(scene) || []).sort(
      (left, right) => left.offset - right.offset,
    );

    for (const record of sceneRecords) {
      if (record.offset < cursor || record.offset >= oldEnd) {
        throw new Error(`Scene ${scene}: overlapping/out-of-range catalog record`);
      }
      const storedLength = input.readUInt16LE(record.offset);
      if (storedLength !== record.byteLength) {
        throw new Error(
          `Scene ${scene} offset 0x${record.offset.toString(16)}: length mismatch`,
        );
      }
      const payloadStart = record.offset + 2;
      const payloadEnd = payloadStart + storedLength;
      const originalText = input.toString("utf8", payloadStart, payloadEnd);
      if (escapeCorpusText(originalText) !== sourceRows[record.corpusIndex]) {
        throw new Error(
          `Scene ${scene} offset 0x${record.offset.toString(16)}: source mismatch`,
        );
      }

      pieces.push(input.subarray(cursor, record.offset));
      pieces.push(encodeStringRecord(translatedRows[record.corpusIndex]));
      cursor = payloadEnd + 1 + (storedLength % 2 === 0 ? 1 : 0);
      replacedOccurrences += 1;
    }
    pieces.push(input.subarray(cursor, oldEnd));

    const payload = Buffer.concat(pieces);
    if (payload.length % 2 !== 0 || payload.length / 2 > 0xffff) {
      throw new Error(`Scene ${scene}: rebuilt bytecode size is invalid`);
    }
    const header = Buffer.alloc(2);
    header.writeUInt16LE(payload.length / 2, 0);
    const block = Buffer.concat([header, payload]);
    outputBlocks.push(block);
    outputOffset += block.length;
  }

  const output = Buffer.concat([prefix, ...outputBlocks]);
  fs.mkdirSync(path.dirname(outputScenes), { recursive: true });
  fs.writeFileSync(outputScenes, output);
  const translationLines = sourceRows.map(
    (source, index) => `${source}\t${translatedRows[index]}`,
  );
  fs.writeFileSync(outputTranslations, `${translationLines.join("\n")}\n`, "utf8");
  const textOverlays = buildTextFileOverlays(translatedRows);

  const manifest = {
    format: 1,
    strategy: "CreateFile IAT redirection; original game files remain untouched",
    generatedAt: new Date().toISOString(),
    sourceCsvRows: sourceRows.length,
    runtimeTranslationEntries: translationLines.length,
    scenes: sceneCount,
    replacedOccurrences,
    textFileOverlays: textOverlays,
    original: {
      path: "KoDp/scenes.dat",
      bytes: input.length,
      sha256: sha256(input),
    },
    overlay: {
      path: "overlay/KoDp/scenes.dat",
      bytes: output.length,
      sha256: sha256(output),
    },
  };
  fs.writeFileSync(outputManifest, `${JSON.stringify(manifest, null, 2)}\n`);

  console.log(
    `Rebuilt ${sceneCount} scenes, replaced ${replacedOccurrences} string occurrences.`,
  );
  console.log(
    `scenes.dat: ${input.length} -> ${output.length} bytes (${manifest.overlay.sha256})`,
  );
  console.log(`Runtime translation database: ${outputTranslations}`);
  console.log(
    `Text overlays: ${textOverlays.generatedFiles} files, ${textOverlays.replacedRecords} records.`,
  );
}

main();
