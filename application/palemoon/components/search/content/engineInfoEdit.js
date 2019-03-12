/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * Based on "Add to Search Bar" addon by Malte Kraus and Gavin Sharp */
const {utils: Cu, classes: Cc, interfaces: Ci} = Components;
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/FileUtils.jsm");


////////////////////////////////////////////////////////////////////////////////
let params = window.arguments[0];


////////////////////////////////////////////////////////////////////////////////
let stringBundle = false;


function getStringBundle () {
  if (!stringBundle) stringBundle = Services.strings.createBundle(bundleUrl);
  return stringBundle;
}


function getString (name) {
  return getStringBundle().GetStringFromName(name);
}


////////////////////////////////////////////////////////////////////////////////
function storeChanges (pars) {
  let eng = params.engine.originalEngine.wrappedJSObject;
  //if (eng._readOnly && !("_serializeToJSON" in eng)) return;

  params.iconURI = document.getElementById("icon").src;

  let url = eng._getURLOfType("text/html");
  url.__new_method = pars.method;
  url.__new_template = pars.url;
  let newParams = new (Cu.getGlobalForObject(eng)).Array(); // don't leak this window
  let QueryParameter = Cu.getGlobalForObject(eng).QueryParameter;
  for (let p of pars.fields) newParams.push(new QueryParameter(p.name, p.value));
  url.__new_params = newParams;
}


////////////////////////////////////////////////////////////////////////////////
const CANVAS_DATAURL_PREFIX = "data:image/png;base64,";
const MAX_ICON_SIZE = 10000;


function pickIcon (e) {
  let prefs = Services.prefs.getBranch("extensions.addtosearchbox.");
  let icon = document.getElementById("icon");

  let fp = Cc["@mozilla.org/filepicker;1"].createInstance(Ci.nsIFilePicker);
  fp.init(window, "", Ci.nsIFilePicker.modeOpen);
  fp.appendFilters(Ci.nsIFilePicker.filterAll | Ci.nsIFilePicker.filterImages);
  fp.filterIndex = 1; // images
  try {
    fp.displayDirectory = prefs.getComplexValue("lastdir", Ci.nsILocalFile);
    if (!fp.displayDirectory) throw new Error("jump to catch!");
  } catch (ex) {
    fp.displayDirectory = FileUtils.getDir("Home", []);
  }

  if (icon.file && icon.file instanceof Ci.nsILocalFile) {
    if (icon.file.parent.exists()) fp.displayDirectory = icon.file.parent;
    if (icon.file.exists()) fp.defaultString = icon.file.leafName;
  }

  let rv = fp.show();
  if (rv != Ci.nsIFilePicker.returnOK) return;
  let file = fp.file;
  prefs.setComplexValue("lastdir", Ci.nsILocalFile, file.parent);
  if (!file.exists() || !file.isReadable() || file.isDirectory()) return;
  params.iconChanged = true;
  setIcon(fp.fileURL.spec, true);
  icon.file = file;
}


function setIcon (url, verbose, fallback) {
  let image = new Image();
  image.onload = function () {
    let icon = document.getElementById("icon");
    let ctx = icon.getContext("2d");
    ctx.fillStyle = ctx.strokeStyle = "rgba(0,0,0,0)"; // transparent
    ctx.clearRect(0, 0, this.width, this.height);
    ctx.drawImage(image, 0, 0, 16, 16);
    let size = atob(icon.toDataURL().substr(CANVAS_DATAURL_PREFIX.length)).length;
    if (size > MAX_ICON_SIZE) {
      Cu.reportError("Add to Search Bar:pickIcon icon too large ("+url+")\n");
      if (verbose) {
        let bundleUrl = "chrome://browser/locale/engineInfos.properties";
        let stringBundle = Services.strings.createBundle(bundleUrl);
        let title = getString("iconFileSizeTitle");
        let sizes = [(MAX_ICON_SIZE/1024).toFixed(2), (size/1024).toFixed(2)];
        let text = getStringBundle().formatStringFromName("iconFileSizeMessage", sizes, 2);
        Services.prompt.alert(window, title, text);
      }
      if ((fallback || icon.src) != url) setIcon(fallback || icon.src, false);
    } else {
      icon.src = icon.toDataURL();
      icon.setAttribute("tooltiptext", url);
    }
  };
  image.src = url;
}


////////////////////////////////////////////////////////////////////////////////
function alert (msg) {
  let pps = Cc["@mozilla.org/embedcomp/prompt-service;1"].getService(Ci.nsIPromptService);
  pps.alert(window, getString(errorAlertTitle), getString(msg));
}


function engEditInfoDlgAccept (e) {
  function $ (name) document.getElementById(name).value;

  // returns object:
  //   .method
  //   .url
  //   .charset
  //   .fields[]
  //     .name
  //     .value
  function parseQText (qtext) {
    let ioSvc = Cc["@mozilla.org/network/io-service;1"].getService(Ci.nsIIOService);

    function uriFromUrl (url, base) {
      let baseUri = null;
      if (typeof(base) === "string") baseUri = uriFromUrl(base);
      else if (base) baseUri = base;
      try {
        return ioSvc.newURI(url, null, baseUri);
      } catch (e) {
        return null;
      }
    }

    function unesc (s) {
      let res = "";
      let pos = 0;
      while (pos < s.length) {
        if (pos+1 < s.length && s[pos] == '\\') {
          res += s[pos+1];
          pos += 2;
        } else {
          res += s[pos];
          ++pos;
        }
      }
      return res;
    }

    let res = {};
    let lines = [];
    for (let s of qtext.split("\n")) {
      s = s.replace(/^\s+/, "").replace(/\s+$/, "");
      if (s.length != 0 && s[0] != '#') lines.push(s);
    }
    if (lines.length < 1) return "invalid query (it's empty)"; // alas

    let qmt = lines[0].match(/^(\S+)\s+(.+)$/);
    if (!qmt) return "invalid query: "+lines[0];

    res.method = qmt[1];
    if (res.method != "GET" && res.method != "POST") return "invalid method: "+res.method;

    res.url = qmt[2];
    if (!uriFromUrl(res.url)) return "invalid URL: "+res.url;

    res.charset = (lines.length > 1 ? lines[1] : "UTF-8");

    let fields = [];
    for (let s of lines.slice(2)) {
      if (s[0] == '=') return "invalid field: "+s;
      // find '=' (but ignore escaped chars)
      let p = 1;
      while (p < s.length) {
        if (s[p] == '\\') {
          p += 2;
        } else if (s[p] == '=') {
          break;
        } else {
          ++p;
        }
      }
      if (p >= s.length) return "invalid field: "+s;
      let fld = {
        name: unesc(s.substr(0, p)),
        value: unesc(s.substr(p+1)),
      };
      fields.push(fld);
    }
    res.fields = fields;
    return res;
  }

  params.name = $("name");
  if (params.name.length == 0) { alert("errorEmptyName"); return false; }

  params.alias = $("alias");
  if (params.alias.length > 0) {
    // check if we already has bookmark or engine with such alias
    try {
      let bmserv = Cc["@mozilla.org/browser/nav-bookmarks-service;1"].getService(Ci.nsINavBookmarksService);
      if (bmserv.getURIForKeyword(alias.value)) { alert("errorBookmarkAlias"); return false; }
    } catch (e) {}
    let engines = params.estore.engines;
    for each (let engine in engines) {
      if (engine.name != params.origName) {
        if (engine.alias == params.alias) { alert("errorEngineAlias"); return false; }
        if (engine.name == params.name) { alert("errorEngineName"); return false; }
      }
    }
  }

  // parse query
  let qp = parseQText($("qtext"));
  if (typeof(qp) == "string") { alert("errorEngineQuery"); return false; }

  params.qp = qp;
  params.accepted = true;

  try {
    storeChanges(params.qp);
  } catch (e) {
    Components.utils.reportError(e);
  }

  return true;
}


function engEditInfoDlgInit () {
  params.origName = params.name;
  params.iconChanged = false;
  document.getElementById("name").value = params.name;
  document.getElementById("alias").value = params.alias;
  document.getElementById("qtext").value = params.qtext;
  setIcon(params.iconURI, false, "moz-icon://stock/gtk-find?size=menu");
  sizeToContent();
  //document.getElementById("alias").focus();
  document.getElementById("alias").select();
}
