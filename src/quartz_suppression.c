#include <ApplicationServices/ApplicationServices.h>

/*
 * Quartz normally suppresses local hardware input for 0.25 seconds after a
 * synthetic event is posted. This program emits synthetic scroll events every
 * few milliseconds during a TrackPoint gesture, so leaving that default in
 * place can keep real pointer motion suppressed briefly after the final scroll
 * event. Disable the legacy process/session suppression interval for the
 * synthetic events emitted by this helper.
 *
 * A future cleanup can move all event creation onto an explicit CGEventSource
 * and set its suppression interval instead; this global API is sufficient for
 * the current single-purpose process and lets us validate the diagnosis without
 * disturbing the working scrolling/cursor-pin paths.
 */
__attribute__((constructor)) static void
macos_trackpoint_disable_quartz_local_event_suppression(void)
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    (void)CGSetLocalEventsSuppressionInterval(0.0);
#pragma clang diagnostic pop
}
