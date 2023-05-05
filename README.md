# Nimble Steps

Nimble Steps writes and reads steps for a gameplay simulation.

* `NbsSteps` for a buffer that has steps in order without any gaps.
* `NbsPendingSteps` for a buffer that can receive steps in any order within a window and keep track of a receive bitmask.
