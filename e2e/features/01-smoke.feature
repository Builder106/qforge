Feature: Home page loads with all the expected sections
  As a recruiter who just clicked a link in a portfolio
  I should see the hero, the demo player, the runner, and the architecture
  so I can tell what this project is in under five seconds.

  Background:
    Given I am on the qforge home page

  Scenario: Hero section renders with name and CTAs
    Then I see the heading "qforge"
    And  I see a link "Run it in your browser"
    And  I see a link "View on GitHub →"

  Scenario: The four headline stats are visible
    Then I see "unit tests passing"
    And  I see "MFLOP/s on one core"
    And  I see "lines of C"
    And  I see "dependencies"

  Scenario: The three main sections are present
    Then I see the heading "Watch it run"
    And  I see the heading "Run it yourself"
    And  I see the heading "How it works"
