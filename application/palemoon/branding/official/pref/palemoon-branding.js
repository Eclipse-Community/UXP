#filter substitution
#filter emptyLines
#include ../../shared/pref/preferences.inc
#include ../../shared/pref/uaoverrides.inc

pref("app.vendorURL", "https://eclipse.cx");
pref("app.releaseNotesURL", "https://github.com/Eclipse-Community/UXP/releases");
pref("browser.lun3r.brand", true);

// Updates disabled
pref("app.update.enabled", false);
pref("app.update.url", "");
