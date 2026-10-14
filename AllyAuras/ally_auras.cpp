// Ally Auras - SFSF mod: friendly auras (Aura of Fast Fighting, Strength, Endurance, Healing, ...) also affect
// allied players and their units, not only the caster's own units.
//
// All auras run spell effect 0x49. This mod registers its own handler for that effect: a copy of SFSF's
// effect_aura (src/internal/handlers/sf_spelleffect_handlers.cpp) where the allied-aura condition
//     target.owner == source.owner
// is replaced by
//     figuresCheckFriendly(source, target)
// Hostile auras behave exactly as before.
//
// Derived from the Spellforce Spell Framework (GPLv3), https://github.com/muddykat-tech/Spellforce-Spell-Framework
// Every player in a multiplayer game must use the same mods.

#include <windows.h>
#include <stdio.h>
#include <vector>
#include "../SFSF/src/api/sfsf.h"

#define AURA_EFFECT_ID 0x49

SpellforceSpellFramework *sfsf;
SpellFunctions *spellAPI;
ToolboxFunctions *toolboxAPI;
FigureFunctions *figureAPI;
IteratorFunctions *iteratorAPI;
RegistrationFunctions *registrationAPI;
EffectFunctions *effectAPI;

static void __thiscall apply_aura_effect(SF_CGdSpell *_this, uint16_t spell_index, uint16_t sub_spell_index,
                                         uint16_t source_index, uint16_t target_index)
{
    SF_GdSpell *spell = &_this->active_spell_list[spell_index];
    SF_Rectangle rect;
    SF_Coord center = {0, 0};
    spellAPI->getTargetsRectangle(_this, &rect, spell_index, 0, &center);
    SF_CGdTargetData target_data;
    target_data.entity_index = target_index;
    target_data.entity_type = 1;
    target_data.position = {0, 0};

    SF_CGdTargetData source_data;
    source_data.entity_index = source_index;
    source_data.entity_type = 1;
    source_data.position = {0, 0};
    SF_Coord caster_pos = _this->SF_CGdFigure->figures[source_index].position;
    SF_Coord target_pos = _this->SF_CGdFigure->figures[target_index].position;
    uint32_t distance = toolboxAPI->getDistance(&caster_pos, &target_pos);
    distance = (distance * 1400) / 4000;
    if (distance == 0)
    {
        distance = 1;
    }
    uint16_t effect_index = effectAPI->addEffect(_this->SF_CGdEffect, kGdEffectAuraResolve,
                                                 &source_data, &target_data,
                                                 _this->OpaqueClass->current_step, distance,
                                                 &rect);
    effectAPI->setEffectXData(_this->SF_CGdEffect, effect_index, EFFECT_SPELL_INDEX, spell_index);
    effectAPI->setEffectXData(_this->SF_CGdEffect, effect_index, EFFECT_SPELL_ID, spell->spell_id);
    effectAPI->setEffectXData(_this->SF_CGdEffect, effect_index, EFFECT_SUBSPELL_ID, sub_spell_index);
    effectAPI->setEffectXData(_this->SF_CGdEffect, effect_index, EFFECT_ENTITY_TYPE, 1);
    effectAPI->setEffectXData(_this->SF_CGdEffect, effect_index, EFFECT_ENTITY_INDEX, source_index);
    effectAPI->setEffectXData(_this->SF_CGdEffect, effect_index, EFFECT_ENTITY_TYPE2, 1);
    effectAPI->setEffectXData(_this->SF_CGdEffect, effect_index, EFFECT_ENTITY_INDEX2, target_index);
}

void __thiscall ally_aura_effect(SF_CGdSpell *_this, uint16_t spell_index)
{
    SF_GdSpell *spell = &_this->active_spell_list[spell_index];
    SF_CGdResourceSpell spell_data;
    uint16_t source_index = spell->source.entity_index;
    spellAPI->getResourceSpellData(_this->SF_CGdResource, &spell_data, spell->spell_id);
    uint16_t sub_effect_id = spell_data.params[6];
    SF_CGdResourceSpell sub_spell_data;
    spellAPI->getResourceSpellData(_this->SF_CGdResource, &sub_spell_data, sub_effect_id);
    _this->active_spell_list[spell_index].to_do_count = (spell_data.params[0] * 10) / 1000;

    SF_CGdTargetData source_data;
    source_data.entity_index = source_index;
    source_data.entity_type = 1;
    source_data.position = {0, 0};
    if (spell->source.entity_type == 1)
    {
        if ((_this->SF_CGdFigure->figures[source_index].owner != (uint16_t)(-1)) &&
            ((*(uint8_t *)(&_this->SF_CGdFigure->figures[source_index].flags) & 0xa) == 0))
        {
            uint32_t tick = spellAPI->addToXData(_this, spell_index, SPELL_TICK_COUNT_AUX, 1);
            if (tick == 1)
            {
                _this->SF_CGdFigure->figures[source_index].flags =
                    (GdFigureFlags)(_this->SF_CGdFigure->figures[source_index].flags | AURA_RUNNING);
                SF_Rectangle rect = {0, 0};
                uint16_t effect_index = effectAPI->addEffect(_this->SF_CGdEffect, kGdEffectSpellHitTarget,
                                                             &source_data, &source_data,
                                                             _this->OpaqueClass->current_step, 0, &rect);

                spellAPI->setXData(_this, spell_index, EFFECT_EFFECT_INDEX, effect_index);
                effectAPI->setEffectXData(_this->SF_CGdEffect, effect_index, EFFECT_SPELL_INDEX, spell_index);
                effectAPI->setEffectXData(_this->SF_CGdEffect, effect_index, EFFECT_SPELL_ID, spell->spell_id);
            }
        }

        uint16_t current_mp = figureAPI->getCurrentStat(_this->SF_CGdFigure, source_index, MANA);
        uint16_t aura_target_type = spell_data.params[5];
        uint16_t manacost = spell_data.params[8];
        if (_this->SF_CGdFigure->figures[source_index].set_type == 7)
        {
            manacost -= ((manacost * 33) / 100);
        }
        if (current_mp >= manacost)
        {
            SF_Coord caster_pos = _this->SF_CGdFigure->figures[source_index].position;
            CGdFigureIterator iter;
            iteratorAPI->setupFigureIterator(&iter, _this);
            iteratorAPI->iteratorSetArea(&iter, &caster_pos, spell_data.params[2]);
            std::vector<uint16_t> targets;
            uint16_t target_index = iteratorAPI->getNextFigure(&iter);
            while (target_index != 0)
            {
                if ((_this->SF_CGdFigure->figures[target_index].owner != (uint16_t)(-1)) &&
                    ((*(uint8_t *)(&_this->SF_CGdFigure->figures[target_index].flags) & 0xa) == 0))
                {
                    //allied auras
                    if (aura_target_type == 1)
                    {
                        if (!toolboxAPI->figuresCheckHostile(_this->SF_CGdFigureToolBox, source_index, target_index))
                        {
                            // The only change against SFSF/vanilla: any friendly figure, not just the caster's own.
                            if (toolboxAPI->figuresCheckFriendly(_this->SF_CGdFigureToolBox, source_index, target_index))
                            {
                                if (spell->spell_line == kGdSpellLineAuraHealing)
                                {
                                    uint16_t missing_hp = _this->SF_CGdFigure->figures[target_index].health.missing_val;
                                    if ((missing_hp > sub_spell_data.params[0]) &&
                                        (!toolboxAPI->hasSpellOnIt(_this->SF_CGdFigureToolBox, target_index,
                                                                   kGdSpellLineRemediless)))
                                    {
                                        targets.push_back(target_index);
                                    }
                                    target_index = iteratorAPI->getNextFigure(&iter);
                                    continue;
                                }

                                if (spell->spell_line == kGdSpellLineAuraRegeneration)
                                {
                                    uint16_t missing_hp = _this->SF_CGdFigure->figures[target_index].health.missing_val;
                                    if ((missing_hp != 0) &&
                                        (!toolboxAPI->hasSpellOnIt(_this->SF_CGdFigureToolBox, target_index,
                                                                   sub_spell_data.spell_line_id)))
                                    {
                                        targets.push_back(target_index);
                                    }
                                    target_index = iteratorAPI->getNextFigure(&iter);
                                    continue;
                                }

                                if (!toolboxAPI->hasSpellOnIt(_this->SF_CGdFigureToolBox, target_index,
                                                              sub_spell_data.spell_line_id))
                                {
                                    if (figureAPI->isWarrior(_this->SF_CGdFigure, target_index))
                                    {
                                        targets.push_back(target_index);
                                    }
                                }
                            }
                        }
                    }
                    //hostile auras
                    if (aura_target_type == 2)
                    {
                        if (toolboxAPI->figuresCheckHostile(_this->SF_CGdFigureToolBox, source_index, target_index))
                        {
                            if (toolboxAPI->isTargetable(_this->SF_CGdFigureToolBox, target_index))
                            {
                                if (!toolboxAPI->hasSpellOnIt(_this->SF_CGdFigureToolBox, target_index,
                                                              sub_spell_data.spell_line_id))
                                {
                                    targets.push_back(target_index);
                                }
                            }
                        }
                    }
                }
                target_index = iteratorAPI->getNextFigure(&iter);
            }
            if (!targets.empty())
            {
                uint16_t aura_target = 0;
                uint16_t prio = 0;
                if (spell->spell_line == kGdSpellLineAuraHealing)
                {
                    for (uint32_t j = 0; j < targets.size(); j++)
                    {
                        target_index = targets.at(j);
                        SF_Coord target_pos = _this->SF_CGdFigure->figures[target_index].position;
                        uint16_t dx = 7;
                        uint16_t sec_prio = 0;
                        for (int i = 8; i > 0; i--)
                        {
                            uint16_t near_x = *(uint16_t *)((uint32_t)&_this->SF_CGdWorld->unknown1[0].uknwn1 + dx) +
                                              target_pos.X;
                            uint16_t near_y = *(uint16_t *)((uint32_t)&_this->SF_CGdWorld->unknown1[0].uknwn2 + dx) +
                                              target_pos.Y;
                            if (((*(uint8_t *)&_this->SF_CGdWorld->cells[near_y * 0x400 + near_x].world_cell_flags) &
                                 0x10) != 0)
                            {
                                uint16_t sec_target = toolboxAPI->getFigureFromWorld(_this->SF_CGdWorldToolBox, near_x,
                                                                                     near_y, 1);

                                if ((sec_target != 0) && (toolboxAPI->figuresCheckHostile(_this->SF_CGdFigureToolBox,
                                                                                          target_index, sec_target)))
                                {
                                    sec_prio++;
                                }
                            }
                            dx += 7;
                        }
                        sec_prio *= (100 - figureAPI->getCurrentHealthPercent(_this->SF_CGdFigure, target_index));
                        if (sec_prio > prio)
                        {
                            prio = sec_prio;
                            aura_target = targets[j];
                        }
                    }
                }
                if (spell->spell_line == kGdSpellLineAuraRegeneration)
                {
                    uint16_t sec_prio = 0;
                    uint16_t prio = 0;

                    for (uint32_t j = 0; j < targets.size(); j++)
                    {
                        target_index = targets.at(j);
                        sec_prio = (100 - figureAPI->getCurrentHealthPercent(_this->SF_CGdFigure, target_index));
                        if (sec_prio > prio)
                        {
                            prio = sec_prio;
                            aura_target = target_index;
                        }
                    }
                }
                if (aura_target == 0)
                {
                    uint16_t value = spellAPI->getRandom(_this->OpaqueClass, targets.size() - 1);
                    aura_target = targets.at(value);
                }
                apply_aura_effect(_this, spell_index, sub_effect_id, source_index, aura_target);
                figureAPI->subMana(_this->SF_CGdFigure, source_index, manacost);
            }
            iteratorAPI->disposeFigureIterator(&iter);
            return;
        }
        else
        {
            return;
        }
    }
    spellAPI->setXData(_this, spell_index, EFFECT_EFFECT_INDEX, 0);
    spellAPI->setEffectDone(_this, spell_index, 0);
}

/***
 * Required by SFSF: called once on load.
 ***/
extern "C" __declspec(dllexport) void InitModule(SpellforceSpellFramework *framework)
{
    sfsf = framework;
    spellAPI = sfsf->spellAPI;
    toolboxAPI = sfsf->toolboxAPI;
    figureAPI = sfsf->figureAPI;
    iteratorAPI = sfsf->iteratorAPI;
    registrationAPI = sfsf->registrationAPI;
    effectAPI = sfsf->effectAPI;

    // Effect handlers are keyed by effect id, so any aura line can carry the link. The tags repeat the vanilla
    // registration of this line (SFSF reads tags from the first registration of a line anyway).
    SFSpell *aura = registrationAPI->registerSpell(kGdSpellLineAuraFastFighting);
    registrationAPI->applySpellTag(aura, SpellTag::AURA_SPELL);
    registrationAPI->applySpellTag(aura, SpellTag::WHITE_AURA_SPELL);
    registrationAPI->linkEffectHandler(aura, AURA_EFFECT_ID, &ally_aura_effect);
}

/***
 * Required by SFSF: describes the mod in logs and the mod list.
 ***/
extern "C" __declspec(dllexport) SFMod *RegisterMod(SpellforceSpellFramework *framework)
{
    return framework->createModInfo("Ally Auras", "1.0.0", "Somka",
                                    "Friendly auras also affect allied players and their units.");
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    return TRUE;
}
