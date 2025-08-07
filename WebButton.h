// -----------------------------------------------------------------------------
// WebButton.h  – action-button widget compatible with WebDisplayBase
// -----------------------------------------------------------------------------
#ifndef WEBBUTTON_H
#define WEBBUTTON_H

#include <Arduino.h>
#include <functional>
#include "WebDisplay.h"

/**
 * Simple push-button that triggers a user-supplied callback once per click.
 *
 *  • add it to your UI with BasicWebInterface::addDisplay()
 *  • no periodic polling → updateIntervalSecs is forced to 0
 *  • optional client-side cooldown avoids accidental double-clicks
 */
class WebButton : public WebDisplayBase {
public:
    /**
     * @param id         unique id + URL path ("/" + id)
     * @param label      text shown on the HTML button
     * @param onClick    function to call when user clicks
     * @param cooldownMs minimum time between two clicks (client + server)
     */
    WebButton(const String& id,
              const String& label,
              std::function<void()> onClick,
              uint32_t cooldownMs = 0)
    : WebDisplayBase(id, /*updateIntervalSecs=*/0),
      label_(label),
      onClick_(std::move(onClick)),
      cooldownMs_(cooldownMs)
    {}

    /* --------------------------------------------------------------------- */
    /* WebDisplayBase interface                                              */
    /* --------------------------------------------------------------------- */

    /** Button page fragment (HTML + inline JS) */
    String createHtmlFragment() const override;

    /** GET handler – fires callback & returns a JSON ack */
    String routeText() const override;

private:
    String                     label_;
    std::function<void()>      onClick_;
    uint32_t                   cooldownMs_;      // ms
    mutable uint32_t           lastClick_   = 0; // ms since boot
    mutable uint32_t           clickCounter_ = 0;
};

#endif // WEBBUTTON_H
