#filter substitution
#filter emptyLines
#include ../../shared/pref/preferences.inc
#include ../../shared/pref/uaoverrides.inc

pref("startup.homepage_override_url","https://eclipse.cx");
pref("app.releaseNotesURL", "https://github.com/Eclipse-Community/UXP");

// Updates disabled
pref("app.update.enabled", false);
pref("app.update.url", "");
