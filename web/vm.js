/* Independent, bounded browser microscope for the unchanged byte VM.
   The C++ verifier remains the authority; this file is cross-tested against it. */
(function(root){'use strict';
const names=['NOP','LOAD_A','LOAD_B','LOAD_IMM','MOV','ADD','SUB','XOR','AND','OR','SHL1','SHR1','INC','DEC','LOAD_MEM','STORE_MEM','CMP_EQ','CMP_LT','SKIP_IF_ZERO','SKIP_IF_NONZERO','EMIT'];
const tasks=['XOR','ADD','SUB','AND','OR','MAX','MIN','EQ','PARITY','ROTATE_XOR'];
function hex(s){if(!/^(?:[0-9a-f]{2})*$/.test(s))throw Error('Invalid genome hex');return (s.match(/../g)||[]).map(x=>parseInt(x,16));}
function decode(s){const fields=s.split('|'),pair=fields.shift().split(':');if(pair.length!==2)throw Error('Invalid genome');
 const program=hex(pair[0]),lang=hex(pair[1]),blocks=fields.map(hex);
 if(lang.length!==128||lang.some(x=>x>=21)||program.length<1||program.length>96||blocks.length>16)throw Error('Invalid genome limits');
 blocks.forEach((b,i)=>{if(b.length<2||b.length>8||b.some(t=>t>=32+i))throw Error('Invalid block graph');});
 const language=Array.from({length:32},(_,i)=>lang.slice(i*4,i*4+4));const flat=[],origins=[],uses=blocks.map(()=>0);let depth=0;
 function visit(t,path,top){if(t<32){if(flat.length>=96)throw Error('Expanded budget exceeded');flat.push(t);origins.push({top,path});return;}
  const i=t-32;if(i>=blocks.length||path.length>=8)throw Error('Invalid module depth');uses[i]++;depth=Math.max(depth,path.length+1);blocks[i].forEach(x=>visit(x,[...path,i],top));}
 program.forEach((t,i)=>visit(t,[],i));if(flat.length<4)throw Error('Program too short');return {program,language,blocks,flat,origins,uses,depth};
}
function target(t,a,b){switch(t){case 0:return a^b;case 1:return(a+b)&255;case 2:return(a-b)&255;case 3:return a&b;case 4:return a|b;case 5:return Math.max(a,b);case 6:return Math.min(a,b);case 7:return +(a===b);case 8:{let x=a^b;x^=x>>4;x^=x>>2;x^=x>>1;return x&1;}case 9:return(((a<<1)|(a>>7))^b)&255;default:throw Error('Invalid task');}}
function run(c,a,b,trace=false){if(!Number.isInteger(a)||!Number.isInteger(b)||a<0||a>255||b<0||b>255)throw Error('Inputs must be integers from 0 to 255');
 let x=0,y=0,last=0,emitted=false;const mem=new Uint8Array(32),steps=[];
 for(let pc=0;pc<c.flat.length;){const code=c.flat[pc];let skip=false;
  c.language[code].forEach((op,slot)=>{const before={x,y,last:emitted?last:null};let note='',address=null;
   switch(op){case 0:note='Leave the state unchanged.';break;
    case 1:x=a;note='Read input A.';break;case 2:x=b;note='Read input B.';break;
    case 3:x=code;note='Load the primitive opcode number, not the block number.';break;
    case 4:y=x;note='Copy the working value into the other register.';break;
    case 5:x=(x+y)&255;note='Add, wrapping at 256.';break;case 6:x=(x-y)&255;note='Subtract, wrapping at 256.';break;
    case 7:x^=y;note='Bitwise XOR: retain bits that differ.';break;case 8:x&=y;note='Bitwise AND: retain shared bits.';break;case 9:x|=y;note='Bitwise OR: combine set bits.';break;
    case 10:x=(x<<1)&255;note='Shift one bit left.';break;case 11:x>>=1;note='Shift one bit right.';break;
    case 12:x=(x+1)&255;note='Increment, wrapping at 256.';break;case 13:x=(x-1)&255;note='Decrement, wrapping at 256.';break;
    case 14:address=y%32;x=mem[address];note='Read memory['+address+'].';break;case 15:address=y%32;mem[address]=x;note='Write memory['+address+'].';break;
    case 16:x=+(x===y);note='Compare equality; write 1 or 0.';break;case 17:x=+(x<y);note='Compare ordering; write 1 if x < y.';break;
    case 18:if(x===0)skip=true;note=x===0?'Skip the next primitive instruction after this opcode finishes.':'No skip requested.';break;
    case 19:if(x!==0)skip=true;note=x!==0?'Skip the next primitive instruction after this opcode finishes.':'No skip requested.';break;
    case 20:last=x;emitted=true;note='Emit a provisional answer; a later EMIT can replace it.';break;
   }
   if(trace)steps.push({pc,code,slot,op:names[op],before,after:{x,y,last:emitted?last:null},skip,note,address,origin:c.origins[pc]});
  });pc+=skip?2:1;
 }
 return {output:emitted?last:x,steps};
}
const api={names,tasks,decode,target,run};if(typeof module!=='undefined'&&module.exports)module.exports=api;root.DriftVM=api;
})(typeof globalThis!=='undefined'?globalThis:this);
