"""Prove each proposed release-policy call belongs to a terminating LVGL debug assertion."""
import argparse,hashlib,json,struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_ARM
from capstone.arm64 import ARM64_OP_IMM,ARM64_OP_REG

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('elf',type=Path)
parser.add_argument('--report',type=Path)
args=parser.parse_args()
path=args.elf;data=path.read_bytes()
assert hashlib.sha256(data).hexdigest()=='2cf51b704a1d750f269f4132f108197ea7ec3b4b0865b070b87e150bf9d0e472'
with path.open('rb') as f:
    elf=ELFFile(f);section=elf.get_section_by_name('.text');raw=section.data();base=section['sh_addr']
    segments=[(s['p_vaddr'],s['p_offset'],s['p_filesz']) for s in elf.iter_segments() if s['p_type']=='PT_LOAD']
md=Cs(CS_ARCH_ARM64,CS_MODE_ARM);md.detail=True
instructions={i.address:i for i in md.disasm(raw,base)}
def string(address):
    for va,off,size in segments:
        if va<=address<va+size:
            at=off+address-va
            return data[at:data.index(0,at)].decode()
    raise ValueError(hex(address))
def value(operand,regs):
    return operand.imm if operand.type==ARM64_OP_IMM else regs[md.reg_name(operand.reg)]
def failure_message(pc):
    regs={}
    for _ in range(80):
        ins=instructions[pc];ops=ins.operands;m=ins.mnemonic
        if m=='adrp':regs[ins.reg_name(ops[0].reg)]=ops[1].imm
        elif m in ('add','sub'):
            regs[ins.reg_name(ops[0].reg)]=value(ops[1],regs)+(1 if m=='add' else -1)*value(ops[2],regs)
        elif m=='mov':regs[ins.reg_name(ops[0].reg)]=value(ops[1],regs)
        elif m=='b':
            assert ops[0].imm!=pc,'Loop before error report'
            pc=ops[0].imm;continue
        elif m=='bl':
            assert ops[0].imm==0x4f050,(hex(pc),'not the vendor error logger')
            nxt=instructions[pc+4]
            assert nxt.mnemonic=='b' and nxt.operands[0].imm==pc+4,'not a terminating assertion'
            assert regs['w0']==3
            return {'message':string(regs['x6']),'expression':string(regs['x5']),
                    'file':string(regs['x1']),'line':regs['w2']}
        else:raise ValueError((hex(pc),m,ins.op_str))
        pc+=4
    raise ValueError('unbounded assertion block')
targets={0x5fef0:'object',0xbd020:'memory'}
rows=[];rejected=[]
for ins in instructions.values():
    if ins.mnemonic!='bl' or ins.operands[0].imm not in targets:continue
    kind=targets[ins.operands[0].imm];pc=ins.address+4
    try:
        if instructions[pc].mnemonic=='mov':pc+=4
        if ins.address==0xa1910:
            assert instructions[pc].mnemonic=='add' and instructions[pc+8].mnemonic=='mov'
            pc+=4
            branch=instructions[pc+8]
        else:branch=instructions[pc+4]
        compare=instructions[pc]
        if kind=='object':
            assert (compare.mnemonic=='tst' and compare.op_str=='w0, #0xff') or (compare.mnemonic=='ands' and compare.op_str.endswith(', w0, #0xff'))
            assert branch.mnemonic in ('b.eq','b.ne')
            fail=branch.operands[0].imm if branch.mnemonic=='b.eq' else branch.address+4
        else:
            assert compare.mnemonic=='cmp' and compare.op_str=='w0, #1'
            assert branch.mnemonic in ('b.eq','b.ne')
            fail=branch.operands[0].imm if branch.mnemonic=='b.ne' else pc+8
        evidence=failure_message(fail)
        assert evidence['message']==('The object is invalid, deleted or corrupted?' if kind=='object' else 'Memory integrity error')
        rows.append({'offset':hex(ins.address),'word':hex(struct.unpack('<I',ins.bytes)[0]),'kind':kind,**evidence})
    except (AssertionError,KeyError,ValueError) as error:
        rejected.append({'offset':hex(ins.address),'kind':kind,'reason':str(error)})
report={'sha256':hashlib.sha256(data).hexdigest(),'text_start':hex(base),'text_size':hex(len(raw)),
        'counts':{kind:sum(row['kind']==kind for row in rows) for kind in targets.values()},
        'sites':rows,'rejected':rejected}
if args.report:args.report.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='sites'},indent=2))
assert report['counts']=={'object':398,'memory':8} and not rejected
