// VaultInk debug page: the parts with no page and no serial port in them (pure functions).
// The page (debug.html) loads this file with a <script> tag and uses the global DebugLib;
// test/debuglib_test.js loads the same file with Node. It ports the parsing of
// scripts/devlink.py: JSON reply lines mixed with log lines, and SCREENSHOT_START:<n>, n raw
// bytes, SCREENSHOT_END.
(function (root, factory) {
  "use strict";
  const lib = factory();
  if (typeof module === "object" && module.exports) module.exports = lib;
  else root.DebugLib = lib;
})(typeof self !== "undefined" ? self : this, function () {
  "use strict";

  const PANEL_WIDTH = 800;
  const PANEL_HEIGHT = 480;
  const FRAMEBUFFER_BYTES = (PANEL_WIDTH / 8) * PANEL_HEIGHT; // 48,000
  const MAX_SCREENSHOT_BYTES = 4 * 1024 * 1024; // as devlink.py: a larger count is not a screenshot
  const MAX_LINE_BYTES = 8192; // a "line" longer than this is cut here (noise, or bytes that lost their marker)
  const MAX_LINES_BEFORE_END = 20; // log lines allowed between the picture's bytes and SCREENSHOT_END

  // ---- lines and replies (devlink.py: parse_json_line, parse_reply, parse_screenshot_start) ----

  // The JSON object of one line, or null for a log line or anything else.
  function parseJsonLine(line) {
    const text = String(line).trim();
    if (!(text.startsWith("{") && text.endsWith("}"))) return null;
    let value;
    try {
      value = JSON.parse(text);
    } catch (error) {
      return null;
    }
    return value !== null && typeof value === "object" && !Array.isArray(value) ? value : null;
  }

  // The reply object of one line (it has a boolean "ok"), or null. A JSON line without "ok" is
  // not a reply.
  function parseReply(line) {
    const reply = parseJsonLine(line);
    return reply !== null && typeof reply.ok === "boolean" ? reply : null;
  }

  // Whether `reply` can be the answer to the command `text` ("STATUS", "LOG 2", ...). A reply
  // that comes after its command timed out (STATUS waits for the screen to finish drawing) must
  // not be taken for the answer to the next command, so each command's reply is known by the
  // fields only it has. An error reply ({"ok":false,"error":...}) names no command and is taken
  // by whichever command is waiting.
  const has = function (object, key) {
    return Object.prototype.hasOwnProperty.call(object, key);
  };
  function replyMatches(text, reply) {
    if (reply === null || typeof reply !== "object" || typeof reply.ok !== "boolean") return false;
    if (reply.ok === false) return true;
    const name = String(text).trim().split(/\s+/)[0];
    switch (name) {
      case "STATUS":
        return has(reply, "version") && has(reply, "uptime_ms");
      case "PING":
        return reply.pong === true;
      case "HELP":
        return has(reply, "commands");
      case "LOG":
        return has(reply, "level");
      case "BENCH":
        return has(reply, "minutes") && has(reply, "file");
      case "REBOOT":
        return reply.action === "reboot";
      case "BOOTLOADER":
        return reply.action === "bootloader";
      default:
        return true; // a command this page does not know: any reply
    }
  }

  // The byte count of a SCREENSHOT_START:<n> line, or null.
  function parseScreenshotStart(line) {
    const match = /^SCREENSHOT_START:([0-9]{1,10})$/.exec(String(line).trim());
    return match ? parseInt(match[1], 10) : null;
  }

  // The line a command is sent as. A bare newline first ends any stray fragment the reader holds.
  function commandBytes(text) {
    return new TextEncoder().encode("\nCMD:" + text + "\n");
  }

  // ---- the stream: lines, with the picture's raw bytes in between ----

  // Takes the port's bytes in whatever pieces they arrive. Calls
  //   onLine(text)            for every line (without its line end; "\r" dropped at the end),
  //   onScreenshot(bytes)     once a whole picture came and SCREENSHOT_END followed it,
  //   onError(message)        when a picture was announced but did not arrive whole.
  // After SCREENSHOT_START:<n> exactly n bytes are taken as they are (they may hold newlines),
  // then lines again; a log line or two may come before SCREENSHOT_END, as devlink.py allows.
  function createStreamParser(handlers) {
    const onLine = handlers.onLine || function () {};
    const onScreenshot = handlers.onScreenshot || function () {};
    const onError = handlers.onError || function () {};
    const decoder = new TextDecoder("utf-8"); // not fatal: a bad byte becomes U+FFFD
    let lineBytes = [];
    let picture = null; // Uint8Array while the raw bytes are being taken
    let pictureUsed = 0;
    let waitingEnd = null; // the finished picture, until SCREENSHOT_END
    let linesSincePicture = 0;
    let skipLine = false; // drop bytes up to the next newline
    let discard = 0; // bytes of an abandoned picture still to throw away
    let markerAt = 0; // how much of END_MARKER the discarded bytes have matched
    const END_MARKER = [83, 67, 82, 69, 69, 78, 83, 72, 79, 84, 95, 69, 78, 68, 10]; // "SCREENSHOT_END\n"

    function finishLine() {
      let length = lineBytes.length;
      while (length > 0 && lineBytes[length - 1] === 13) --length;
      const text = decoder.decode(Uint8Array.from(lineBytes.slice(0, length)));
      lineBytes = [];
      if (waitingEnd !== null) {
        if (text.trim() === "SCREENSHOT_END") {
          const done = waitingEnd;
          waitingEnd = null;
          onLine(text);
          onScreenshot(done);
          return;
        }
        if (++linesSincePicture > MAX_LINES_BEFORE_END) {
          waitingEnd = null;
          onError("SCREENSHOT_END did not follow the picture: bytes were lost on the way");
        }
      }
      const size = parseScreenshotStart(text);
      onLine(text);
      if (size === null) return;
      if (waitingEnd !== null) {
        waitingEnd = null;
        onError("a new picture started before SCREENSHOT_END of the last one");
      }
      if (size <= 0 || size > MAX_SCREENSHOT_BYTES) {
        onError("the reader announced " + size + " bytes: not a picture");
        return;
      }
      picture = new Uint8Array(size);
      pictureUsed = 0;
    }

    function feed(chunk) {
      let at = 0;
      while (at < chunk.length) {
        if (discard > 0) {
          // The rest of a picture nobody waits for: thrown away up to its announced length, or
          // up to SCREENSHOT_END when that comes first (bytes were lost on the way).
          const byte = chunk[at++];
          --discard;
          markerAt = byte === END_MARKER[markerAt] ? markerAt + 1 : byte === END_MARKER[0] ? 1 : 0;
          if (markerAt === END_MARKER.length) discard = 0;
          if (discard === 0) {
            markerAt = 0;
            skipLine = markerAt === 0 && byte !== 10;
          }
          continue;
        }
        if (picture !== null) {
          const take = Math.min(picture.length - pictureUsed, chunk.length - at);
          picture.set(chunk.subarray(at, at + take), pictureUsed);
          pictureUsed += take;
          at += take;
          if (pictureUsed === picture.length) {
            waitingEnd = picture;
            linesSincePicture = 0;
            picture = null;
          }
          continue;
        }
        const byte = chunk[at++];
        if (skipLine) {
          // What is left of a line that was cut: dropped up to its newline.
          if (byte === 10) skipLine = false;
          continue;
        }
        if (byte === 10) {
          finishLine();
        } else {
          lineBytes.push(byte);
          if (lineBytes.length >= MAX_LINE_BYTES) finishLine();
        }
      }
    }

    // Forget a half line or a half picture: the port closed, or the picture took too long.
    // Returns true when a picture was thrown away.
    function reset() {
      const lost = picture !== null || waitingEnd !== null;
      lineBytes = [];
      picture = null;
      pictureUsed = 0;
      waitingEnd = null;
      discard = 0;
      markerAt = 0;
      skipLine = false;
      return lost;
    }

    // Give up on the picture that is arriving (it took too long) without losing the place in
    // the stream: the bytes still to come are thrown away, then lines are read again from the
    // next newline. Unlike reset(), for a port that is still open.
    function abandonPicture() {
      if (picture !== null) {
        discard = picture.length - pictureUsed + END_MARKER.length;
        markerAt = 0;
        picture = null;
        pictureUsed = 0;
      } else if (waitingEnd !== null) {
        waitingEnd = null;
      }
    }

    return {
      feed: feed,
      reset: reset,
      abandonPicture: abandonPicture,
      // What the parser is in the middle of, for a timeout on the page.
      state: function () {
        if (discard > 0) return { mode: "discard", left: discard };
        if (picture !== null) return { mode: "bytes", got: pictureUsed, want: picture.length };
        if (waitingEnd !== null) return { mode: "end" };
        return { mode: "lines" };
      },
    };
  }

  // ---- the picture (devlink.py: rotate_framebuffer) ----

  // Turns the panel's framebuffer into the upright picture: rows of packed bits, top row first.
  // The same turn as ScreenshotUtil::saveFramebufferAsBmp and devlink.py: the picture is `height`
  // wide and `width` tall, and its pixel (x, y) is the framebuffer's pixel (y, height - 1 - x).
  // Bit 7 of a byte is the leftmost pixel; 1 is white.
  function rotateFramebuffer(data, width, height) {
    if (width === undefined) width = PANEL_WIDTH;
    if (height === undefined) height = PANEL_HEIGHT;
    const widthBytes = Math.floor(width / 8);
    if (width % 8 !== 0 || data.length !== widthBytes * height) {
      throw new Error(data.length + " bytes is not a " + width + "x" + height + " 1-bit framebuffer");
    }
    const outWidth = height;
    const outRowBytes = Math.floor((outWidth + 7) / 8);
    const rows = [];
    for (let y = 0; y < width; ++y) {
      const row = new Uint8Array(outRowBytes);
      const sourceByte = y >> 3;
      const sourceShift = 7 - (y % 8);
      for (let x = 0; x < outWidth; ++x) {
        const sourceRow = height - 1 - x;
        if ((data[sourceRow * widthBytes + sourceByte] >> sourceShift) & 1) row[x >> 3] |= 0x80 >> (x % 8);
      }
      rows.push(row);
    }
    return { width: outWidth, height: width, rows: rows };
  }

  // The upright picture as RGBA bytes for a canvas (ImageData): black or white, opaque.
  function framebufferToRgba(data, width, height) {
    const picture = rotateFramebuffer(data, width, height);
    const rgba = new Uint8ClampedArray(picture.width * picture.height * 4);
    let at = 0;
    for (let y = 0; y < picture.height; ++y) {
      const row = picture.rows[y];
      for (let x = 0; x < picture.width; ++x) {
        const value = (row[x >> 3] >> (7 - (x % 8))) & 1 ? 255 : 0;
        rgba[at++] = value;
        rgba[at++] = value;
        rgba[at++] = value;
        rgba[at++] = 255;
      }
    }
    return { width: picture.width, height: picture.height, rgba: rgba };
  }

  // ---- STATUS as a table ----

  const STATUS_LABELS = Object.assign(Object.create(null), {
    version: "Firmware version",
    build: "Build (dev-user: the read-only developer build)",
    uptime_ms: "Since it started (a wake from sleep is a start)",
    heap_free: "Free memory now (bytes)",
    heap_min_free: "Lowest free memory so far (bytes)",
    heap_largest: "Largest free block (bytes)",
    psram_free: "Free PSRAM (bytes)",
    battery_mv: "Battery (millivolts, with the cable in)",
    battery_percent: "Battery (percent, with the cable in)",
    usb: "USB cable in",
    charging: "Charging",
    wake_cause: "Why it woke (0 power key, 1 after flashing, 2 USB power, 3 other, 4 key not held)",
    esp_wake_cause: "The chip's wake cause number",
    reset_reason: "The chip's reset reason number",
    cpu_mhz: "CPU speed now (MHz)",
    light_present: "Has a frontlight",
    light_on: "Frontlight on",
    light_brightness: "Frontlight brightness",
    light_warmth: "Frontlight warmth",
    activity: "Screen in front",
    sleep_timeout_ms: "Auto-sleep after (0 is never)",
    idle_ms: "Since the last input",
    log_debug: "Debug log lines printing",
    bench_minutes: "SD bench log interval (minutes, 0 is off)",
    dropped_lines: "Command lines dropped for being too long",
  });

  function formatDuration(ms) {
    if (typeof ms !== "number" || !isFinite(ms) || ms < 0) return String(ms);
    const seconds = Math.floor(ms / 1000);
    if (seconds < 60) return ms + " ms (" + seconds + " s)";
    const minutes = Math.floor(seconds / 60);
    if (minutes < 60) return ms + " ms (" + minutes + " min " + (seconds % 60) + " s)";
    return ms + " ms (" + Math.floor(minutes / 60) + " h " + (minutes % 60) + " min)";
  }

  // [{key, label, value}] in the order the reader sent the fields; "ok" is left out.
  function statusRows(reply) {
    const rows = [];
    if (reply === null || typeof reply !== "object") return rows;
    for (const key of Object.keys(reply)) {
      if (key === "ok" || !has(reply, key)) continue;
      const raw = reply[key];
      let value;
      if (typeof raw === "boolean") value = raw ? "yes" : "no";
      else if (/_ms$/.test(key)) value = formatDuration(raw);
      else if (raw !== null && typeof raw === "object") value = JSON.stringify(raw);
      else value = String(raw);
      rows.push({ key: key, label: STATUS_LABELS[key] || "", value: value });
    }
    return rows;
  }

  // ---- redaction (best effort: the page shows the result in a box the user can edit) ----

  // File endings of books, notes and pictures: a name with one of these is someone's file.
  const CONTENT_EXT =
    "epub|pdf|txt|md|markdown|cbz|cbr|mobi|azw3|azw|fb2|xtc|xtch|djvu|doc|docx|chm|html|htm|xhtml|" +
    "bmp|png|jpg|jpeg|gif|canvas|excalidraw|opf|ncx|css|json|csv|tsv|cfg|bin|log|zip|kepub|sdr";
  // Endings that make "a.b" a file or a piece of code, not a host name.
  const NOT_A_TLD = new Set(
    (CONTENT_EXT + "|cpp|c|h|hpp|cc|py|js|sh|ini|yml|yaml|toml|xml|ttf|otf|dat|db|tmp|bak|old|lock|key|pem|pub").split("|")
  );

  function escapeRegExp(text) {
    return text.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  }

  // Returns { text, counts }. counts says how many of each token were put in.
  // options.extraWords: words or names of the user's own to hide as <hidden> (whole text, any case).
  // options.counts: an object to count into (to add up several calls).
  function redact(text, options) {
    const counts = (options && options.counts) || {};
    const put = function (token) {
      counts[token] = (counts[token] || 0) + 1;
      return "<" + token + ">";
    };
    let out = String(text);

    // The user's own list first, before anything else changes the text.
    const extra = ((options && options.extraWords) || [])
      .map(function (word) {
        return String(word).trim();
      })
      .filter(function (word) {
        return word.length >= 2;
      })
      .sort(function (a, b) {
        return b.length - a.length;
      });
    for (const word of extra) {
      out = out.replace(new RegExp(escapeRegExp(word), "gi"), function () {
        return put("hidden");
      });
    }

    // URLs: the scheme stays (it says what kind of server), the host and the rest go.
    out = out.replace(/\b([a-z][a-z0-9+.-]{1,15}):\/\/([^\s"'<>]+)/gi, function (match, scheme, rest) {
      const slash = rest.search(/[/?#]/);
      const tail = slash >= 0 && rest.length > slash + 1 ? "/" + put("path") : "";
      return scheme + "://" + put("host") + tail;
    });

    // E-mail addresses (any letters, not only ASCII), then user@host without a dot (lef@mynas).
    out = out.replace(/[^\s@<>"'()\[\],;:=]+@[^\s@<>"'()\[\],;:=]+\.[^\s@<>"'()\[\],;:=.]+/g, function () {
      return put("email");
    });
    out = out.replace(/[^\s@<>"'()\[\],;:=/]+@[^\s@<>"'()\[\],;:=/.]+/g, function () {
      return put("user") + "@" + put("host");
    });

    // Hardware addresses (Wi-Fi BSSID, Bluetooth), before IPv6 which they resemble; also twelve
    // hex digits in a row with no separators.
    out = out.replace(/\b[0-9a-f]{2}(?:[:-][0-9a-f]{2}){5}\b/gi, function () {
      return put("mac");
    });
    out = out.replace(/(?<![\w.])[0-9a-f]{12}(?![\w.])/gi, function (match) {
      return /^\d+$/.test(match) ? match : put("mac"); // twelve decimal digits are a number
    });

    // IPv6: with "::", or four or more groups (a time like 12:34:56 has three).
    out = out.replace(
      /(?<![\w:.])(?:(?:[0-9a-f]{1,4}:){1,7}:(?:[0-9a-f]{1,4}(?::[0-9a-f]{1,4}){0,6})?|::(?:[0-9a-f]{1,4}:){0,6}[0-9a-f]{1,4}|(?:[0-9a-f]{1,4}:){3,7}[0-9a-f]{1,4})(?![\w:])/gi,
      function () {
        return put("ip");
      }
    );

    // IPv4 (a version like 0.74.0 has three numbers and is kept).
    out = out.replace(/(?<![\w.])(?:\d{1,3}\.){3}\d{1,3}(?![\w.]*\.\d)(?!\w)/g, function () {
      return put("ip");
    });

    // Wi-Fi names, in the wording the firmware's log uses, and the general "connected to <name>".
    // The name runs to the end of the line, or to " (" or a comma: a name may hold blanks.
    const NAME = "([^\\r\\n]+?)(?=\\s*(?:$|\\(|,|;))";
    // "(SSID: name)" at the end of a line: the name may itself hold brackets.
    out = out.replace(/(\(SSID:\s*)([^\r\n]+)(\)[ \t]*\r?)$/gim, function (match, lead, name, tail) {
      return lead + put("ssid") + tail;
    });
    const ssidPatterns = [
      "(\\bSSID\\s*[:=]\\s*)" + "([^\\r\\n)]+?)(?=\\s*(?:$|\\)|,|;))",
      "(\\bjoined\\s+(?:Wi-?Fi\\s+|network\\s+|SSID\\s+)?)" + NAME,
      "(\\bWi-?Fi network\\s*:\\s*)" + NAME,
      "(\\bcredentials for\\s*:?\\s*)" + NAME,
      "(\\bpassword for\\s+)" + NAME,
      "(\\bsaved network(?: failed)?\\s*:\\s*)" + NAME,
      "(\\bconnected to\\s+(?:Wi-?Fi\\s+|network\\s+|SSID\\s+)?)" + NAME,
    ];
    for (const pattern of ssidPatterns) {
      out = out.replace(new RegExp(pattern, "gim"), function (match, lead, name) {
        if (/^<[a-z]+>/.test(name.trim())) return match; // already a token (an address)
        return lead + put("ssid");
      });
    }

    // A line of a Wi-Fi scan list: "<name> -67 dBm" or "3: <name> (-67 dBm)".
    out = out.replace(/((?:^|\] )\s*(?:\d+[:.)]\s+)?)([^\r\n\]]+?)(\s+\(?-\d{2,3}\s?dBm\b)/gim, function (match, lead, name, tail) {
      if (/^<[a-z]+>$/.test(name.trim()) || /\bRSSI\b|:\s*$/i.test(name)) return match;
      return lead + put("ssid") + tail;
    });

    // A server or a user named in passing: "host mynas", "user lef", "server=nas".
    out = out.replace(/(\b(?:host|hostname|server)(?:\s*=\s*|\s+))([^\s<>,;()=:]+)(?![\w:=])/g, function (match, lead, name) {
      return lead + put("host");
    });
    out = out.replace(/(\b(?:user|username)(?:\s*=\s*|\s+))([^\s<>,;()=:]+)(?![\w:=])/g, function (match, lead, name) {
      return lead + put("user");
    });

    // A server named after "connecting to": host or host:port.
    out = out.replace(/(\bconnecting to\s+)([^\s<>,;()]+)/gi, function (match, lead, host) {
      const port = /:\d{1,5}$/.exec(host);
      return lead + put("host") + (port ? port[0] : "");
    });

    // Paths on the SD card: from a "/" at the start of a word. A name may hold blanks, so when a
    // known file ending comes later on the line the path runs up to it; the ending is kept.
    const pathPattern = new RegExp(
      "(?<![\\w<>:/.\\\\])/(?![/\\s])[^\\s\"'<>]*(?:(?: [^\\s\"'<>]+)*?\\.(" + CONTENT_EXT + ")\\b)?",
      "gi"
    );
    out = out.replace(pathPattern, function (match, ending) {
      if (match.length < 2) return match; // a lone "/"
      let kept = ending ? "." + ending.toLowerCase() : "";
      if (!kept) {
        const simple = new RegExp("\\.(" + CONTENT_EXT + ")$", "i").exec(match.replace(/[),.;:]+$/, ""));
        if (simple) kept = "." + simple[1].toLowerCase();
      }
      const trailing = /[),;:]+$/.exec(match);
      return put("path") + kept + (trailing && !ending ? trailing[0] : "");
    });

    // File names without a folder: inside quotes (the whole name), then a bare word.
    out = out.replace(new RegExp("([\"'])([^\"'\\r\\n<>]+)\\.(" + CONTENT_EXT + ")\\1", "gi"), function (match, quote, name, ending) {
      return quote + put("file") + "." + ending.toLowerCase() + quote;
    });
    // A path that did not start with "/" (Vaults/Notes/My Plan.md): from the first word that
    // holds a "/".
    out = out.replace(new RegExp("(?<![\\w<>.])[^\\s\"'<>]*/[^\\s\"'<>]*(?: [^\\s\"'<>]+)*?\\.(" + CONTENT_EXT + ")\\b(?![\\w>])", "gi"), function (match, ending) {
      return put("path") + "." + ending.toLowerCase();
    });
    // A bare file name may hold blanks ("Tolstoy - War and Peace.epub"): the whole name is
    // taken, from the start of the message (after "] ", ": ", "= " or "-> ") up to the ending.
    out = out.replace(new RegExp("(?:^|(?<=\\] )|(?<=: )|(?<== )|(?<=-> ))[^\\r\\n\\]:<>\"'=]*?[^\\s\\]:<>\"'=/]\\.(" + CONTENT_EXT + ")\\b(?![\\w>])", "gim"), function (match, ending) {
      return put("file") + "." + ending.toLowerCase();
    });
    // What is left: one word with a file ending.
    out = out.replace(new RegExp("(?<![\\w<>.])[^\\s\"'<>]+\\.(" + CONTENT_EXT + ")\\b(?![\\w>])", "gi"), function (match, ending) {
      return put("file") + "." + ending.toLowerCase();
    });

    // Names the firmware's log prints after a label, in its own wording.
    // key=value: title=War and Peace, file=..., src=..., plugin='x' (to the end of the line when
    // the value is not quoted: the firmware puts these last).
    out = out.replace(/(\b(?:title|author|name|file|filename|path|src|plugin|folder|dir|book|note|vault|label)=)('[^'\r\n]*'|"[^"\r\n]*"|[^\r\n]+)/gi, function (match, lead, value) {
      const bare = value.replace(/^['"]|['"]$/g, "").trim();
      if (bare === "" || /^<[a-z]+>(\.\w+)?$/.test(bare) || /^-?\d+$/.test(bare)) return match;
      const quote = /^['"]/.test(value) ? value[0] : "";
      return lead + quote + put("name") + quote;
    });
    // A name in single quotes ('%s' in the firmware's messages).
    out = out.replace(/(?<![\w<>])'([^'\r\n]{1,200})'(?![\w>])/g, function (match, value) {
      if (/^<[a-z]+>(\.\w+)?$/.test(value.trim())) return match;
      return "'" + put("name") + "'";
    });
    // Messages with the name in the middle.
    const middle = [
      /(\bIndex )([^\r\n]+?)(: [^\r\n]*\d+ notes\b)/g,
      /((?:^|\] ))([^\r\n\]]+?)(: \d+ notes, \d+ read\b)/gm,
      /(\bRename of )([^\r\n]+?)(: \d+ links\b)/g,
      /(\bRecovered )([^\r\n]+?)( from its temp file\b)/g,
      /(\b(?:Vector family|Family|font family|in family) )([^\r\n]+?)( has no\b|: |$)/gm,
      /(\bNo )([^\r\n]+?)( directory on SD card\b)/g,
    ];
    for (const pattern of middle) {
      out = out.replace(pattern, function (match, lead, name, tail) {
        if (/^<[a-z]+>(\.\w+)?$/.test(name.trim())) return match;
        return lead + put("name") + tail;
      });
    }
    // "<label>: <name>" at the end of a log line, where the label says a name follows (Title:,
    // Author:, "Added server:", "Loaded 3 bookmarks for book:", "Found dictionary:" ...). Not
    // for JSON lines and not for the report's "key: value" STATUS lines.
    const LABEL = "title|author|book|file|filename|folder|directory|dir|path|name|server|user|network|note|" +
      "family|image|plugin|dictionary|source|link|credentials|URL|host|vault|document|entry|reference|rejected|" +
      "duplicate|found|listing page for|changed|open failed|skipping|cover|font";
    out = out.replace(new RegExp("^((?!\\s*\\{)[^\\r\\n]*(?<!<)\\b(?:" + LABEL + ")\\b(?!>)[^:\\r\\n]*:[ \\t]+)([^\\r\\n]+?)([ \\t]*\\r?)$", "gim"), function (match, lead, value, tail) {
      const bare = value.trim();
      // Already a token (with what the firmware adds after it), or only a number.
      if (/^<[a-z]+>/.test(bare) || /^-?[\d.]+( \w+)?$/.test(bare) || /^["']<[a-z]+>/.test(bare) || /["']<[a-z]+>(\.\w+)?["']$/.test(bare)) return match;
      if (!/^\[\d+\] \[/.test(lead)) return match; // only the reader's log lines: "[ms] [LEVEL] [TAG] ..."
      return lead + put("name") + tail;
    });

    // Host names: words joined by dots that end in letters (name.local, sync.example.org).
    out = out.replace(/(?<![\w<>.@/-])(?:[a-z0-9](?:[a-z0-9-]*[a-z0-9])?\.)+([a-z]{2,24})(?![\w(-])/gi, function (match, tld) {
      if (NOT_A_TLD.has(tld.toLowerCase())) return match;
      return put("host");
    });

    return { text: out, counts: counts };
  }

  // ---- the report ----

  // Plain text, built from what the user ticked. `parts`:
  //   when          a date text (the page passes the local time)
  //   note          what the user typed about the problem ("" for none)
  //   status        the STATUS reply object, or null when the reader did not answer one
  //   includeStatus
  //   logLines      array of log lines (all the page holds)
  //   includeLog, lastLines
  //   screenshots   file names of the pictures the user ticked (they are saved as separate files)
  //   clean         optional: a function applied to what came from the reader or the user (the
  //                 note, the STATUS lines, the log lines), not to the report's own words
  function buildReport(parts) {
    const clean = typeof parts.clean === "function" ? parts.clean : function (value) { return value; };
    const lines = [];
    lines.push("VaultInk debug report");
    lines.push("Made with the VaultInk debug page on " + (parts.when || "an unknown date") + ".");
    const status = parts.status || null;
    lines.push("Firmware version: " + (status && status.version ? clean(String(status.version)) : "unknown (no STATUS reply)"));
    lines.push("");
    if (parts.note && String(parts.note).trim()) {
      lines.push("== What happened ==");
      lines.push(clean(String(parts.note).trim()));
      lines.push("");
    }
    if (parts.includeStatus) {
      lines.push("== STATUS ==");
      if (status) {
        for (const row of statusRows(status)) lines.push(row.key + ": " + clean(row.value));
      } else {
        lines.push("(no STATUS reply: the reader has the normal firmware, was asleep, or was not asked)");
      }
      lines.push("");
    }
    if (parts.includeLog) {
      const all = parts.logLines || [];
      const wanted = Math.max(0, Math.floor(Number(parts.lastLines) || 0));
      const tail = wanted > 0 ? all.slice(-wanted) : [];
      lines.push("== Log: the last " + tail.length + " of " + all.length + " lines ==");
      if (tail.length > 0) lines.push(clean(tail.join("\n")));
      lines.push("");
    }
    const shots = parts.screenshots || [];
    if (shots.length > 0) {
      lines.push("== Screenshots ==");
      lines.push("Saved as separate files next to this report. Attach them to the issue yourself:");
      for (const name of shots) lines.push("- " + name);
      lines.push("A picture is not redacted: it shows what was on the screen.");
      lines.push("");
    }
    return lines.join("\n");
  }

  // The report with the redaction pass over everything that came from the reader or the user.
  // Returns { text, counts }. options.extraWords as in redact().
  function buildRedactedReport(parts, options) {
    const counts = {};
    const extraWords = (options && options.extraWords) || [];
    const copy = Object.assign({}, parts, {
      clean: function (value) {
        return redact(value, { extraWords: extraWords, counts: counts }).text;
      },
    });
    return { text: buildReport(copy), counts: counts };
  }

  return {
    PANEL_WIDTH: PANEL_WIDTH,
    PANEL_HEIGHT: PANEL_HEIGHT,
    FRAMEBUFFER_BYTES: FRAMEBUFFER_BYTES,
    MAX_SCREENSHOT_BYTES: MAX_SCREENSHOT_BYTES,
    MAX_LINE_BYTES: MAX_LINE_BYTES,
    parseJsonLine: parseJsonLine,
    parseReply: parseReply,
    parseScreenshotStart: parseScreenshotStart,
    replyMatches: replyMatches,
    commandBytes: commandBytes,
    createStreamParser: createStreamParser,
    rotateFramebuffer: rotateFramebuffer,
    framebufferToRgba: framebufferToRgba,
    statusRows: statusRows,
    formatDuration: formatDuration,
    redact: redact,
    buildReport: buildReport,
    buildRedactedReport: buildRedactedReport,
  };
});
