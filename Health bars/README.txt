Health Bars 1.3.0 - SpellForce Platinum Edition 1.61 (public test beta) + SFSF
=============================================================================

Health bars are always shown above units, not only under the mouse cursor
or on the selection. They are the game's own bars: same look, same size.

INSTALLATION
Copy health_bars.sfm and health_bars.ini into the game's "sfsf" folder.

SETTINGS (health_bars.ini, read when the game starts)
  Own=1          your units and heroes
  Allies=1       allied and neutral units
  Enemies=1      enemies
  Workers=0      your workers (woodcutters, miners...): 0 keeps your base
                 uncluttered
  OnlyDamaged=0  1 = only units that have lost health
  MaxDistance=0  only within this radius around your main character
                 (0 = no limit; try 30 to 50 if big battles slow down)
  RedEnemies=1   enemy bars in red (the game's colour of an almost empty
                 bar), whatever their health; the bar's length still shows
                 it. 0 = game colours.
  GreenOthers=1  your bars and allied bars always green, even when they
                 drop. 0 = game colours (green -> red).
  ToggleKey=0    key that shows/hides the bars in game, as a Windows
                 virtual-key code: 0x48 = H, 0x75 = F6... (0 = always shown)

Units inside buildings get no bar (they are invisible), nor do units hidden
by the fog of war: the mod asks the game the same question it asks to decide
whether to draw a unit. The game's own selection and mouse-over markers are
unchanged.

To check: sfsf.log, line "[health_bars] health bars always shown ...", and
"[health_bars] bar colours: ..." for the colours in use.

Sources: sources\examples\health_bars (build it in the SFSF repository like
the other examples).
