#!/usr/bin/env python3
"""Compile/run the actual chat authorization body with minimal engine fakes.
No core configuration or worldserver build. Also checks the bridge controller.
"""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'src/Ai/Dungeon/DungeonClear/Action/DungeonClearChatActions.cpp').read_text()
start=s.index('    bool IsAuthorized('); end=s.index('{',start)+1; depth=1
while depth:
    depth += (s[end]=='{')-(s[end]=='}'); end+=1
function=s[start:end]
header='\n#include <cassert>\nconstexpr unsigned SEC_GAMEMASTER=2;\nstruct Session { unsigned security=0; unsigned GetSecurity() { return security; } };\nstruct Group { unsigned member=1; bool IsMember(unsigned guid) {return member==guid;} };\nstruct Player { unsigned guid=1; bool bot=false,self=false; Group* group=nullptr; Session* session=nullptr;\n Group* GetGroup(){return group;} Session* GetSession(){return session;} unsigned GetGUID(){return guid;} };\n#define GET_PLAYERBOT_AI(p) ((p)->bot)\nnamespace DcPlayerbotCompat { bool IsSelfBot(Player* p){return p->self;} }\nstruct Event {Player* owner=nullptr; Player* getOwner(){return owner;}};\n'
body='\nint main(){ Group g; Session regular,gm{2}; Player bot{10,true,false,&g,&regular}; Player human{1,false,false,&g,&regular};\n assert(IsAuthorized(&bot,Event{&human})); human.guid=2; assert(!IsAuthorized(&bot,Event{&human}));\n human.session=&gm; assert(IsAuthorized(&bot,Event{&human}));\n human.bot=true; assert(!IsAuthorized(&bot,Event{&human}));\n human.self=true; assert(IsAuthorized(&bot,Event{&human}));\n human.session=&regular; human.guid=1; assert(IsAuthorized(&bot,Event{&human}));\n assert(!IsAuthorized(&bot,Event{})); assert(!IsAuthorized(nullptr,Event{&human}));\n bot.group=nullptr; assert(!IsAuthorized(&bot,Event{&human})); }\n'
with tempfile.TemporaryDirectory(prefix='dc-autonomy-') as tmp:
    source=Path(tmp)/'chat.cpp'; source.write_text(header+function+body)
    runtime=(root/'src/Integration/DcAutonomyRuntime.cpp').read_text()
    begin=runtime.index('std::uint32_t RequiredMask(')
    finish=runtime.index('\nclass EnginePort', begin)
    (Path(tmp)/'AutonomyEvidenceBody.h').write_text(runtime[begin:finish])
    for std in ('c++17','c++20'):
        for src in (source,root/'t/TestAutonomyStandalone.cpp',root/'t/TestAutonomyEvidence.cpp'):
            out=Path(tmp)/'test'
            subprocess.run(['c++','-std='+std,'-O2','-Wall','-Wextra','-Werror','-pedantic','-I'+str(root/'src'),'-I'+tmp,str(src),'-o',str(out)],check=True)
            subprocess.run([str(out)],check=True)
        print('PASS unchanged production IsAuthorized: humans, selfbots, bots, GM, missing owner/group; '+std,flush=True)
