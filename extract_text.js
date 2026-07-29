const fs = require("fs");
const path = require("path");

const projectDir = __dirname;
const gameDir = path.join(projectDir, "Game");
const kodpDir = path.join(gameDir, "KoDp");
const outputPath = path.join(projectDir, "king-of-dragon-pass_text.csv");
const catalogPath = path.join(projectDir, "king-of-dragon-pass_text_catalog.tsv");

const utf8Fatal = new TextDecoder("utf-8", { fatal: true });
const records = [];
const corpus = [];
const corpusIndex = new Map();
const sourceCounts = new Map();

function escapeText(value) {
  return value
    .replaceAll("\\", "\\\\")
    .replaceAll("\r", "\\r")
    .replaceAll("\n", "\\n")
    .replaceAll("\t", "\\t")
    .replace(/[\x00-\x08\x0B\x0C\x0E-\x1F\x7F]/g, (ch) =>
      `\\x${ch.charCodeAt(0).toString(16).toUpperCase().padStart(2, "0")}`,
    );
}

function addText(value, source, locator, kind) {
  if (typeof value !== "string") return;
  const text = escapeText(value);
  if (!/\p{L}/u.test(text)) return;

  let index = corpusIndex.get(text);
  if (index === undefined) {
    index = corpus.length;
    corpusIndex.set(text, index);
    corpus.push(text);
  }

  records.push({ index, source, locator, kind });
  sourceCounts.set(kind, (sourceCounts.get(kind) || 0) + 1);
}

function decodeEntities(value) {
  const named = {
    amp: "&",
    apos: "'",
    gt: ">",
    lt: "<",
    nbsp: " ",
    quot: '"',
  };
  return value.replace(/&(#x[0-9a-f]+|#\d+|[a-z]+);/gi, (whole, entity) => {
    if (entity[0] === "#") {
      const radix = entity[1].toLowerCase() === "x" ? 16 : 10;
      const digits = radix === 16 ? entity.slice(2) : entity.slice(1);
      const codePoint = Number.parseInt(digits, radix);
      return Number.isFinite(codePoint) ? String.fromCodePoint(codePoint) : whole;
    }
    return named[entity.toLowerCase()] ?? whole;
  });
}

function normalizeVisibleHtml(value) {
  return decodeEntities(
    value
      .replace(/<br\s*\/?>/gi, "\n")
      .replace(/<[^>]+>/g, "")
      .replace(/\r\n?/g, "\n"),
  )
    .split("\n")
    .map((line) => line.trim())
    .filter(Boolean)
    .join(" ")
    .replace(/\s{2,}/g, " ")
    .trim();
}

function extractScenesDat() {
  const relative = "KoDp/scenes.dat";
  const filePath = path.join(gameDir, relative);
  const data = fs.readFileSync(filePath);
  if (data.length < 12) throw new Error("scenes.dat is too small");

  const sceneCount = data.readUInt16LE(4);
  const pointerTableEnd = data.readUInt16LE(6);
  if (pointerTableEnd !== 8 + sceneCount * 4) {
    throw new Error(
      `Unexpected scenes.dat pointer table: count=${sceneCount}, end=0x${pointerTableEnd.toString(16)}`,
    );
  }

  for (let sceneId = 1; sceneId <= sceneCount; sceneId += 1) {
    const blockOffset = data.readUInt32LE(4 + sceneId * 4);
    if (blockOffset + 2 > data.length) {
      throw new Error(`Scene ${sceneId} points outside scenes.dat`);
    }

    const wordCount = data.readUInt16LE(blockOffset);
    const blockEnd = blockOffset + 2 + wordCount * 2;
    if (blockEnd > data.length) {
      throw new Error(`Scene ${sceneId} bytecode extends outside scenes.dat`);
    }

    let coveredUntil = -1;
    for (let offset = blockOffset + 2; offset + 3 < blockEnd; offset += 2) {
      const byteLength = data.readUInt16LE(offset);
      const payloadStart = offset + 2;
      const payloadEnd = payloadStart + byteLength;
      if (
        byteLength < 1 ||
        byteLength > 8192 ||
        payloadEnd >= blockEnd ||
        data[payloadEnd] !== 0
      ) {
        continue;
      }

      let value;
      try {
        value = utf8Fatal.decode(data.subarray(payloadStart, payloadEnd));
      } catch {
        continue;
      }

      if (
        value.includes(String.fromCharCode(0)) ||
        !/\p{L}/u.test(value) ||
        offset < coveredUntil
      ) {
        continue;
      }

      const alignedEnd =
        payloadEnd + 1 + (byteLength % 2 === 0 ? 1 : 0);
      coveredUntil = alignedEnd;
      addText(
        value,
        relative,
        `scene=${sceneId};offset=0x${offset.toString(16).toUpperCase()};bytes=${byteLength}`,
        "scenes.dat",
      );
    }
  }

  return sceneCount;
}

function extractOpalMap() {
  const relative = "KoDp/Map.opalmap";
  const filePath = path.join(gameDir, relative);
  const data = fs.readFileSync(filePath);
  let coveredUntil = -1;

  // Map labels use an 8-bit UTF-8 byte length followed by NUL.
  for (let offset = 0; offset + 2 < data.length; offset += 1) {
    const byteLength = data[offset];
    const payloadStart = offset + 1;
    const payloadEnd = payloadStart + byteLength;
    if (
      byteLength < 2 ||
      payloadEnd >= data.length ||
      data[payloadEnd] !== 0
    ) {
      continue;
    }

    let value;
    try {
      value = utf8Fatal.decode(data.subarray(payloadStart, payloadEnd));
    } catch {
      continue;
    }
    if (
      offset < coveredUntil ||
      value.includes(String.fromCharCode(0)) ||
      !/\p{L}/u.test(value)
    ) {
      continue;
    }

    coveredUntil = payloadEnd + 1;
    addText(
      value,
      relative,
      `offset=0x${offset.toString(16).toUpperCase()};bytes=${byteLength}`,
      "Opal map label",
    );
  }
}

function extractHardcodedStrings() {
  const relative = "King of Dragon Pass.s86";
  const filePath = path.join(gameDir, relative);
  const data = fs.readFileSync(filePath);

  // IDA shows the game-owned string pool in this range. At 0x3806C0,
  // Marmalade SDK diagnostics begin, so they are deliberately excluded.
  const start = 0x349d1f;
  const end = Math.min(0x3806c0, data.length);

  for (let offset = start; offset < end; ) {
    const first = data[offset];
    if (first < 0x20 || first > 0x7e) {
      offset += 1;
      continue;
    }

    let cursor = offset;
    while (cursor < end && data[cursor] >= 0x20 && data[cursor] <= 0x7e) {
      cursor += 1;
    }

    if (cursor < end && data[cursor] === 0 && cursor - offset >= 2) {
      const value = data.toString("ascii", offset, cursor);
      const compact = !/\s/.test(value);
      const looksLikePath =
        /[\\/]/.test(value) ||
        /(?:\.png|\.jpe?g|\.mp3|\.ui|\.plist|\.dat|\.xml|\.html|\.kodp)$/i.test(value) ||
        value.includes("://");
      const looksLikeIdentifier =
        compact &&
        (/[a-z][A-Z]/.test(value) ||
          /[_{}]/.test(value) ||
          /(?:Controller|Label|Button|View|Box|Slider|Scroll|Texture|Caption|Wrapper|Handler|Manager)$/.test(
            value,
          ));
      const looksLikeDebug =
        value.startsWith("!!!") ||
        value.startsWith("(invalid ") ||
        value.startsWith("error loading extension:");

      if (
        /\p{L}/u.test(value) &&
        !looksLikePath &&
        !looksLikeIdentifier &&
        !looksLikeDebug
      ) {
        addText(
          value,
          relative,
          `file-offset=0x${offset.toString(16).toUpperCase()}`,
          "s86 hardcoded",
        );
      }
    }

    offset = Math.max(offset + 1, cursor + 1);
  }
}

function extractUiCaptions() {
  const root = path.join(kodpDir, "uiFiles");
  const files = fs
    .readdirSync(root)
    .filter((name) => name.toLowerCase().endsWith(".ui"))
    .sort();

  for (const name of files) {
    const relative = `KoDp/uiFiles/${name}`;
    const content = fs.readFileSync(path.join(root, name), "utf8");
    const regex = /^\s*(?!\/\/)\s*caption\s+"((?:\\.|[^"])*)"/gm;
    let match;
    let ordinal = 0;
    while ((match = regex.exec(content)) !== null) {
      ordinal += 1;
      if (!match[1]) continue;
      const value = match[1]
        .replaceAll("\\n", "\n")
        .replaceAll('\\"', '"')
        .replaceAll("\\\\", "\\");
      addText(value, relative, `caption=${ordinal}`, "UI caption");
    }
  }
}

function extractPlists() {
  const names = ["Intro.plist", "tutorial.plist", "tutorial_ipad.plist"];
  for (const name of names) {
    const relative = `KoDp/${name}`;
    const content = fs.readFileSync(path.join(kodpDir, name), "utf8");
    const regex = /<string>([\s\S]*?)<\/string>/gi;
    let match;
    let ordinal = 0;
    while ((match = regex.exec(content)) !== null) {
      ordinal += 1;
      const value = decodeEntities(match[1]);
      if (/^[A-Z]\d+$/.test(value.trim())) continue;
      addText(value, relative, `string=${ordinal}`, "plist string");
    }
  }
}

function extractXml() {
  const name = "Text.xml";
  const relative = `KoDp/${name}`;
  const content = fs.readFileSync(path.join(kodpDir, name), "utf8");
  const rootMatch = content.match(
    /<LocalizeString>([\s\S]*?)<\/LocalizeString>/i,
  );
  const body = rootMatch ? rootMatch[1] : content;
  const regex = /<([A-Za-z][\w.-]*)>([\s\S]*?)<\/\1>/g;
  let match;
  let ordinal = 0;
  while ((match = regex.exec(body)) !== null) {
    ordinal += 1;
    const value = normalizeVisibleHtml(match[2]);
    addText(value, relative, `${match[1]}=${ordinal}`, "localization XML");
  }
}

function extractHelp() {
  const root = path.join(kodpDir, "Help");
  const files = fs
    .readdirSync(root)
    .filter((name) => name.toLowerCase().endsWith(".txt"))
    .sort();
  for (const name of files) {
    const relative = `KoDp/Help/${name}`;
    const content = fs
      .readFileSync(path.join(root, name), "utf8")
      .replace(/\r\n?/g, "\n");
    const paragraphs = content.split(/\n\s*\n/);
    paragraphs.forEach((value, index) => {
      addText(value.trim(), relative, `paragraph=${index + 1}`, "help text");
    });
  }
}

function walkFiles(root, predicate, output = []) {
  for (const entry of fs.readdirSync(root, { withFileTypes: true })) {
    const fullPath = path.join(root, entry.name);
    if (entry.isDirectory()) walkFiles(fullPath, predicate, output);
    else if (predicate(fullPath)) output.push(fullPath);
  }
  return output;
}

function extractHtml() {
  const roots = [path.join(kodpDir, "Manual"), path.join(kodpDir, "Text")];
  for (const root of roots) {
    const files = walkFiles(root, (file) =>
      file.toLowerCase().endsWith(".html"),
    ).sort();
    for (const filePath of files) {
      const relative = path
        .relative(gameDir, filePath)
        .split(path.sep)
        .join("/");
      const content = fs.readFileSync(filePath, "utf8");
      const regex = /<(title|h[1-6]|p|li|td|th)\b[^>]*>([\s\S]*?)<\/\1>/gi;
      let match;
      let ordinal = 0;
      while ((match = regex.exec(content)) !== null) {
        ordinal += 1;
        const value = normalizeVisibleHtml(match[2]);
        addText(
          value,
          relative,
          `${match[1].toLowerCase()}=${ordinal}`,
          "HTML text",
        );
      }
    }
  }
}

function csvCell(value) {
  return `"${value.replaceAll('"', '""')}"`;
}

function tsvCell(value) {
  return String(value)
    .replaceAll("\\", "\\\\")
    .replaceAll("\t", "\\t")
    .replaceAll("\r", "\\r")
    .replaceAll("\n", "\\n");
}

const sceneCount = extractScenesDat();
extractOpalMap();
extractHardcodedStrings();
extractUiCaptions();
extractPlists();
extractXml();
extractHelp();
extractHtml();

fs.writeFileSync(
  outputPath,
  `${corpus.map(csvCell).join("\r\n")}\r\n`,
  "utf8",
);

const catalogLines = [
  ["corpus_index", "source", "locator", "kind"].join("\t"),
  ...records.map((record) =>
    [
      record.index,
      tsvCell(record.source),
      tsvCell(record.locator),
      tsvCell(record.kind),
    ].join("\t"),
  ),
];
fs.writeFileSync(catalogPath, `${catalogLines.join("\r\n")}\r\n`, "utf8");

console.log(`scenes.dat scene blocks: ${sceneCount}`);
console.log(`unique corpus entries: ${corpus.length}`);
console.log(`catalog provenance rows: ${records.length}`);
for (const [kind, count] of [...sourceCounts.entries()].sort()) {
  console.log(`${kind}: ${count}`);
}
