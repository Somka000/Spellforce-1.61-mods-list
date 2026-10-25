 * Right-clicking a spell on the bottom action bar toggles autocast for it.
 * Toggled spells are cast by the avatar / rune heroes through the same battle
 * AI that already drives the spells of every other unit; a pink outline marks
 * the toggled icons (its colour is set in the ini file). Meditation, which
 * restores all mana, is only cast while the mana is below a share of the
 * maximum set in the ini file (33% by default).
 *
 * Outside fights: some toggled spells (outside_combat in the ini file; by
 * default Healing, Greater Healing, Charisma, Enlightenment and Meditation)
 * are also cast while the avatar or hero stands idle: Meditation below the
 * mana share, the healing spells on the most wounded of the avatar and heroes
 * in range below heal_health_percent of their health, any other spell on the
 * caster once it has run out. The cast is ordered like a player's (it
 * checks only the mana for heroes: cooldowns are checked here).
 *
 * Summons: a toggled summon spell has a number of summons to keep (shown at
 * the icon's top right corner): "+" with a right click raises it, "-" with a
 * right click lowers it (0 turns autocast off). In and out of fights the
 * caster summons again while it has fewer of them; in a fight a due summon
 * goes before what the battle AI chose. Each summon is a spell running with
 * the caster as its source, which is how they are counted.
 *
 * Two things keep the vanilla battle AI from casting for the avatar and heroes:
 *  - their spells are not in the figure's action list (which the AI ranks);
 *    they live in the player's spell slots, mirrored client side in UiDbProxy
 *    together with their cooldowns;
 *  - CGdAiBattleDevelopement::CheckFigureCanDoSpellAction refuses every spell
 *    action of a main character or hero.
 * While the AI ranks actions for one of the local player's heroes, this mod
 * appends the toggled slot spells (with their cooldown) to the figure's action
 * list, lets the refusal through for exactly those spell lines, and removes
 * the injected actions again afterwards. The chosen injected spell is carried
 * out by the vanilla executor (other mods may retarget the hero's attacks), and
 * toggled auras of a hero are ended when its fight is over, like the AI does
 * for every other unit.
### Mod created by AngryHamster228