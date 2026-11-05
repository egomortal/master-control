/* SPDX-License-Identifier: GPL-3.0-or-later */
(function(g){'use strict';const M=(()=>{

function candidates(s){if(!s||!Array.isArray(s.pads)||s.pads.length>16)throw Error('Invalid controller state.');return s.pads.filter(p=>Number.isInteger(p.slot)&&p.slot>=1&&p.slot<=4&&p.transport==='bt').map(p=>({slot:p.slot,name:typeof p.model==='string'?p.model.slice(0,80):'Controller'}))}
function permitted(p,now,last,busy){return !!p&&Number.isInteger(p.slot)&&p.slot>=1&&p.slot<=4&&Number.isFinite(now)&&!busy&&now-last>=2000}
return{candidates,permitted};
})();if(typeof module!=='undefined')module.exports=M;else g.Model=M})(typeof globalThis!=='undefined'?globalThis:this);
