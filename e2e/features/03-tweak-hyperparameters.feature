Feature: Tweaking hyperparameters changes the WASM run
  The collapsible "tweak hyperparameters" panel reads input values and
  passes them as positional argv to Module.callMain. The C side echoes
  the resolved values in the run header — if the round-trip works, the
  override marker appears in the terminal.

  Background:
    Given I am on the qforge home page
    And   I open the tweak hyperparameters panel

  Scenario: Overriding XOR epochs is reflected in the run header
    When I set the "epochs" input under "XOR" to "2000"
    And  I click the "XOR" run button
    Then the terminal eventually shows "Epochs:       2000"

  Scenario: Reset button restores defaults
    When I set the "epochs" input under "XOR" to "1234"
    And  I click "reset to defaults"
    Then the "epochs" input under "XOR" has value "10000"
