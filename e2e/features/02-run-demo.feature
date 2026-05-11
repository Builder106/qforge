Feature: Running a demo in-browser via WebAssembly
  The "Run it yourself" section spawns a Web Worker, loads the WASM, and
  streams stdout into xterm.js. The fastest demo (XOR) is the cheapest
  signal that the whole pipeline still works end-to-end.

  Background:
    Given I am on the qforge home page

  # xterm.js auto-scrolls to the bottom as new lines stream in, so we assert
  # only on content guaranteed to be in the final visible viewport (the
  # prediction table and the "Done" line, both near the end of the run).
  Scenario: XOR runs and prints the prediction table
    When I click the "XOR" run button
    Then the terminal eventually shows "Done"
    And  the terminal contains a prediction row for "[0, 1]"

  Scenario: Stop button cancels a long-running demo
    When I click the "DQN trader" run button
    And  I wait for the terminal to show "Q-Network"
    And  I click the stop button
    Then the terminal eventually shows "killed by user"
