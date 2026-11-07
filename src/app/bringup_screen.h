// Hardware bring-up screen: board info, colour bars, touch tracker, backlight slider.
#pragma once

namespace bringup {

void show();
void tick();   // call from loop() to refresh the live touch readout

}  // namespace bringup
