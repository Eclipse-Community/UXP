/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef builtin_intl_SharedIntlData_h
#define builtin_intl_SharedIntlData_h

#include "mozilla/MemoryReporting.h"

#include <stddef.h>

#include "jsalloc.h"
#include "js/CharacterEncoding.h"
#include "js/GCAPI.h"
#include "js/GCHashTable.h"
#include "js/RootingAPI.h"
#include "js/Utility.h"
#include "vm/String.h"

namespace js {

namespace intl {

/**
 * Stores Intl data which can be shared across compartments (but not contexts).
 *
 * Used for data which is expensive when computed repeatedly or is not
 * available through ICU.
 */
class SharedIntlData
{
    struct LinearStringLookup
    {
        union {
            const JS::Latin1Char* latin1Chars;
            const char16_t* twoByteChars;
        };
        bool isLatin1;
        size_t length;
        JS::AutoCheckCannotGC nogc;
        HashNumber hash = 0;

        explicit LinearStringLookup(JSLinearString* string)
          : isLatin1(string->hasLatin1Chars()), length(string->length())
        {
            if (isLatin1)
                latin1Chars = string->latin1Chars(nogc);
            else
                twoByteChars = string->twoByteChars(nogc);
        }

        LinearStringLookup(const char* chars, size_t length)
          : isLatin1(true), length(length)
        {
            latin1Chars = reinterpret_cast<const JS::Latin1Char*>(chars);
        }
    };

  private:
    /**
     * Information tracking the set of the supported time zone names, derived
     * from the IANA time zone database <https://www.iana.org/time-zones>.
     *
     * There are two kinds of IANA time zone names: Zone and Link (denoted as
     * such in database source files). Zone names are the canonical, preferred
     * name for a time zone, e.g. Asia/Kolkata. Link names simply refer to
     * target Zone names for their meaning, e.g. Asia/Calcutta targets
     * Asia/Kolkata. That a name is a Link doesn't *necessarily* reflect a
     * sense of deprecation: some Link names also exist partly for convenience,
     * e.g. UTC and GMT as Link names targeting the Zone name Etc/UTC.
     *
     * Two data sources determine the time zone names we support: those ICU
     * supports and IANA's zone information.
     *
     * Unfortunately the names ICU and IANA support, and their Link
     * relationships from name to target, aren't identical, so we can't simply
     * implicitly trust ICU's name handling. We must perform various
     * preprocessing of user-provided zone names and post-processing of
     * ICU-provided zone names to implement ECMA-402's IANA-consistent behavior.
     *
     * Also see <https://ssl.icu-project.org/trac/ticket/12044> and
     * <http://unicode.org/cldr/trac/ticket/9892>.
     */

    using TimeZoneName = JSAtom*;

    struct TimeZoneHasher
    {
        struct Lookup : LinearStringLookup
        {
            explicit Lookup(JSFlatString* timeZone);
        };

        static js::HashNumber hash(const Lookup& lookup) { return lookup.hash; }
        static bool match(TimeZoneName key, const Lookup& lookup);
    };

    using TimeZoneSet = js::GCHashSet<TimeZoneName,
                                      TimeZoneHasher,
                                      js::SystemAllocPolicy>;

    using TimeZoneMap = js::GCHashMap<TimeZoneName,
                                      TimeZoneName,
                                      TimeZoneHasher,
                                      js::SystemAllocPolicy>;

    /**
     * As a threshold matter, available time zones are those time zones ICU
     * supports, via ucal_openTimeZones. But ICU supports additional non-IANA
     * time zones described in intl/icu/source/tools/tzcode/icuzones (listed in
     * IntlTimeZoneData.cpp's |legacyICUTimeZones|) for its own backwards
     * compatibility purposes. This set consists of ICU's supported time zones,
     * minus all backwards-compatibility time zones.
     */
    TimeZoneSet availableTimeZones;

    /**
     * IANA treats some time zone names as Zones, that ICU instead treats as
     * Links. For example, IANA considers "America/Indiana/Indianapolis" to be
     * a Zone and "America/Fort_Wayne" a Link that targets it, but ICU
     * considers the former a Link that targets "America/Indianapolis" (which
     * IANA treats as a Link).
     *
     * ECMA-402 requires that we respect IANA data, so if we're asked to
     * canonicalize a time zone name in this set, we must *not* return ICU's
     * canonicalization.
     */
    TimeZoneSet ianaZonesTreatedAsLinksByICU;

    /**
     * IANA treats some time zone names as Links to one target, that ICU
     * instead treats as either Zones, or Links to different targets. An
     * example of the former is "Asia/Calcutta, which IANA assigns the target
     * "Asia/Kolkata" but ICU considers its own Zone. An example of the latter
     * is "America/Virgin", which IANA assigns the target
     * "America/Port_of_Spain" but ICU assigns the target "America/St_Thomas".
     *
     * ECMA-402 requires that we respect IANA data, so if we're asked to
     * canonicalize a time zone name that's a key in this map, we *must* return
     * the corresponding value and *must not* return ICU's canonicalization.
     */
    TimeZoneMap ianaLinksCanonicalizedDifferentlyByICU;

    bool timeZoneDataInitialized = false;

    /**
     * Precomputes the available time zone names, because it's too expensive to
     * call ucal_openTimeZones() repeatedly.
     */
    bool ensureTimeZones(JSContext* cx);

  public:
    /**
     * Returns the validated time zone name in |result|. If the input time zone
     * isn't a valid IANA time zone name, |result| remains unchanged.
     */
    bool validateTimeZoneName(JSContext* cx, JS::HandleString timeZone,
                              JS::MutableHandleString result);

    /**
     * Returns the canonical time zone name in |result|. If no canonical name
     * was found, |result| remains unchanged.
     *
     * This method only handles time zones which are canonicalized differently
     * by ICU when compared to IANA.
     */
    bool tryCanonicalizeTimeZoneConsistentWithIANA(JSContext* cx, JS::HandleString timeZone,
                                                   JS::MutableHandleString result);
  private:
    using Locale = JSAtom*;

    struct LocaleHasher
    {
        struct Lookup : LinearStringLookup
        {
            explicit Lookup(JSLinearString* locale);
            Lookup(const char* chars, size_t length);
        };

        static js::HashNumber hash(const Lookup& lookup) { return lookup.hash; }
        static bool match(Locale key, const Lookup& lookup);
    };

    using LocaleSet = GCHashSet<Locale, LocaleHasher, SystemAllocPolicy>;

    // Set of supported locales for all Intl service constructors except Collator,
    // which uses its own set.
    //
    // UDateFormat:
    // udat_[count,get]Available() return the same results as their
    // uloc_[count,get]Available() counterparts.
    //
    // UNumberFormatter:
    // unum_[count,get]Available() return the same results as their
    // uloc_[count,get]Available() counterparts.
    //
    // UPluralRules and URelativeDateTimeFormatter:
    // We're going to use ULocale availableLocales as per ICU recommendation:
    // https://unicode-org.atlassian.net/browse/ICU-12756
    LocaleSet supportedLocales;

    // ucol_[count,get]Available() return different results compared to
    // uloc_[count,get]Available(), we can't use |supportedLocales| here.
    LocaleSet collatorSupportedLocales;

    bool supportedLocalesInitialized = false;

    // CountAvailable and GetAvailable describe the signatures used for ICU API
    // to determine available locales for various functionality.
    using CountAvailable = int32_t (*)();
    using GetAvailable = const char* (*)(int32_t localeIndex);

    static bool getAvailableLocales(JSContext* cx, LocaleSet& locales,
                                    CountAvailable countAvailable,
                                    GetAvailable getAvailable);

    /**
    * Precomputes the available locales sets.
    */
    bool ensureSupportedLocales(JSContext* cx);

  public:
    enum class SupportedLocaleKind {
        Collator,
        DateTimeFormat,
        NumberFormat,
        PluralRules,
        RelativeTimeFormat
    };

    /**
    * Sets |supported| to true if |locale| is supported by the requested Intl
    * service constructor. Otherwise sets |supported| to false.
    */
    MOZ_MUST_USE bool isSupportedLocale(JSContext* cx, SupportedLocaleKind kind,
                                        JS::Handle<JSString*> locale,
                                        bool* supported);

  private:
    /**
     * The case first parameter (BCP47 key "kf") allows to switch the order of
     * upper- and lower-case characters. ICU doesn't directly provide an API
     * to query the default case first value of a given locale, but instead
     * requires to instantiate a collator object and then query the case first
     * attribute (UCOL_CASE_FIRST).
     * To avoid instantiating an additional collator object whenever we need
     * to retrieve the default case first value of a specific locale, we
     * compute the default case first value for every supported locale only
     * once and then keep a list of all locales which don't use the default
     * case first setting.
     * There is almost no difference between lower-case first and when case
     * first is disabled (UCOL_LOWER_FIRST resp. UCOL_OFF), so we only need to
     * track locales which use upper-case first as their default setting.
     */

    LocaleSet upperCaseFirstLocales;

    bool upperCaseFirstInitialized = false;

    /**
     * Precomputes the available locales which use upper-case first sorting.
     */
    bool ensureUpperCaseFirstLocales(JSContext* cx);

  public:
    /**
     * Sets |isUpperFirst| to true if |locale| sorts upper-case characters
     * before lower-case characters.
     */
    bool isUpperCaseFirst(JSContext* cx, JS::HandleString locale, bool* isUpperFirst);

  public:
    void destroyInstance();

    void trace(JSTracer* trc);

    size_t sizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const;
};

} // namespace intl

} // namespace js

#endif /* builtin_intl_SharedIntlData_h */