/* SPDX-License-Identifier: GPL-3.0-or-later */
(function(g){'use strict';const M=(()=>{

function pads(s){if(!s||!Array.isArray(s.pads)||s.pads.length>16)throw Error('Invalid controller status.');return s.pads.map(p=>{if(!Number.isInteger(p.slot)||p.slot<1||p.slot>16)throw Error('Invalid controller slot.');return{slot:p.slot,model:typeof p.model==='string'?p.model.slice(0,80):'Controller',transport:['bt','usb'].includes(p.transport)?p.transport:'unknown',battery:Number.isInteger(p.battery)&&p.battery>=0&&p.battery<=100?p.battery:null,charging:p.charging===true,firmware:typeof p.firmware==='string'?p.firmware.slice(0,64):null,epoch:typeof p.deviceEpoch==='string'&&p.deviceEpoch.length<=64?p.deviceEpoch:null,vid:/^[0-9a-f]{4}$/i.test(p.vid)?p.vid:null,pid:/^[0-9a-f]{4}$/i.test(p.pid)?p.pid:null}})}
function sample(history,p,now){if(p.battery===null||!p.epoch)return history;if(!Number.isFinite(now)||now<0)throw Error('Invalid sample time.');const same=history.filter(e=>e.epoch===p.epoch);if(same.length&&now-same[same.length-1].time<10000)return same;return same.concat({epoch:p.epoch,time:now,battery:p.battery}).slice(-12)}
return{pads,sample};
})();if(typeof module!=='undefined')module.exports=M;else g.Model=M})(typeof globalThis!=='undefined'?globalThis:this);
