/* SPDX-License-Identifier: MIT
 * Generated from Piglit 62ef6b0db. See THIRD-PARTY.md and tests/import-shader-fork.py. */
static const SrStage sr_stages[]={
{SR_VP,5,
"!!ARBvp1.0\n"
"MOV result.position, vertex.position;\n"
"MOV result.texcoord[0], program.env[0];\n"
"MOV result.texcoord[1], program.env[1];\n"
"END\n"
"\n"},
{SR_FP,12,
"!!ARBfp1.0\n"
"DPH result.color, -fragment.texcoord[0], fragment.texcoord[1];\n"
"MOV result.color.w, 1;\n"
"END\n"
"\n"},
{SR_VP,7,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"END\n"
"\n"},
{SR_FP,12,
"!!ARBfp1.0\n"
"#This tests instructions like: ADD Temp[0], Temp[0], Temp[1] where one of\n"
"#the source registers is the same as the destination register.  These\n"
"#should not be replaced with presubtract operations.\n"
"TEMP color;\n"
"MOV color, {0.1, 0.4, 0.8, 1.0};\n"
"ADD color.x, color.x, color.x;\n"
"ADD color.y, color.x, color.y;\n"
"MOV result.color, color;\n"
"END\n"
"\n"},
{SR_VP,7,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"END\n"
"\n"},
{SR_FP,12,
"!!ARBfp1.0\n"
"#This test is for instructions that read from more components than the\n"
"#presubtract operation writes.  For example:\n"
"#ADD Temp[0].x Temp[1].x, Temp[2].x\n"
"#MUL Temp[4]x. Temp[0].x, Temp[5].x\n"
"#DP3 Temp[6].x Temp[0].xyz, Temp[0].xyz\n"
"#The ADD instruction can be converted to a presubtract operation and be\n"
"#used by the MUL instruction, but it cannot be used by the DP3 instruction\n"
"#since that instruction reads from more than just the x component of Temp[0].\n"
"#Therefore the ADD instruction should not be removed.\n"
"TEMP color;\n"
"MOV color, {0.1, 0.4, 0.8, 1.0};\n"
"SUB color.x, color.x, color.x;\n"
"ADD color.y, color.x, color.y;\n"
"SUB color.z, color.x, color.x;\n"
"DP3 color.w, color, color;\n"
"MOV result.color, color;\n"
"END\n"
"\n"},
{SR_VP,10,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"MOV\tresult.texcoord[0],0;\n"
"MOV\tresult.texcoord[1],1;\n"
"END\n"
"\n"},
{SR_FP,17,
"!!ARBfp1.0\n"
"TEMP\tR0;\n"
"MOV\tR0, fragment.position;\n"
"MOV\tresult.color,fragment.texcoord[1];\n"
"END\n"
"\n"},
{SR_VP,7,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"END\n"
"\n"},
{SR_FP,12,
"!!ARBfp1.0\n"
"CMP result.color, {-1, -1, 0, 1}, {0, 1, 1, 0}, {0, 0, 0, 1};\n"
"END\n"
"\n"},
{SR_VP,7,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"END\n"
"\n"},
{SR_FP,12,
"!!ARBfp1.0\n"
"TEMP temp;\n"
"MOV temp, program.local[0];\n"
"DST temp, temp.xyyw, program.local[1];\n"
"MOV result.color, temp;\n"
"END\n"
"\n"},
{SR_VP,7,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"END\n"
"\n"},
{SR_FP,12,
"!!ARBfp1.0\n"
"TEMP temp;\n"
"MOV temp, program.local[1];\n"
"DST temp, program.local[0], temp.xyzy;\n"
"MOV result.color, temp;\n"
"END\n"
"\n"},
{SR_VP,7,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"END\n"
"\n"},
{SR_FP,12,
"!!ARBfp1.0\n"
"TEMP temp;\n"
"EX2_SAT temp, program.local[0].x;\n"
"MAD result.color, temp, {0.5}.x, {0.25}.x;\n"
"END\n"
"\n"},
{SR_VP,13,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"END\n"
"\n"},
{SR_FP,18,
"!!ARBfp1.0\n"
"ADD result.color, {1, 0, .25, 0}, {0.5, 0, 1, 0};\n"
"END\n"
"\n"},
{SR_VP,5,
"!!ARBvp1.0\n"
"MOV result.position, vertex.position;\n"
"MOV result.texcoord[0], program.env[0];\n"
"MOV result.texcoord[1], program.env[1];\n"
"END\n"
"\n"},
{SR_FP,12,
"!!ARBfp1.0\n"
"KIL fragment.texcoord[0].xxyy;\n"
"KIL fragment.texcoord[1].zwwz;\n"
"\n"
"# green will be neither of the kills occurring, otherwise\n"
"# we'll get the blue clear color.\n"
"MOV result.color, { 0.0, 1.0, 0.0, 0.0 };\n"
"END\n"
"\n"},
{SR_VP,5,
"!!ARBvp1.0\n"
"MOV result.position, vertex.position;\n"
"MOV result.texcoord[0], program.env[0];\n"
"MOV result.texcoord[1], program.env[1];\n"
"MOV result.texcoord[2], program.env[2];\n"
"END\n"
"\n"},
{SR_FP,13,
"!!ARBfp1.0\n"
"TEMP r0;\n"
"LRP_SAT r0, fragment.texcoord[0], fragment.texcoord[1], fragment.texcoord[2];\n"
"MUL result.color, 0.5, r0;\n"
"END\n"
"\n"},
{SR_VP,5,
"!!ARBvp1.0\n"
"TEMP texcoord;\n"
"\n"
"ADD texcoord.xy, vertex.position, {1}.x;\n"
"MUL texcoord.xy, texcoord.xyyy, {0.5}.x;\n"
"MOV texcoord.z, {0}.x;\n"
"MOV texcoord.w, {0}.x;\n"
"\n"
"MOV result.position, vertex.position;\n"
"MOV result.texcoord[0], texcoord;\n"
"END\n"
"\n"},
{SR_FP,18,
"!!ARBfp1.0\n"
"TEX result.color, fragment.texcoord[0], texture[0], 2D;\n"
"END\n"
"\n"},
{SR_VP,5,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"TEMP texcoord;\n"
"\n"
"ADD texcoord.xy, vertex.position, program.env[0].xyyy;\n"
"MUL texcoord.xy, texcoord.xyyy, program.env[0].z;\n"
"MOV texcoord.z, {0}.x;\n"
"MOV texcoord.w, {1}.x;\n"
"\n"
"MOV result.texcoord[0], texcoord;\n"
"END\n"
"\n"},
{SR_FP,18,
"!!ARBfp1.0\n"
"TXB result.color, fragment.texcoord[0], texture[0], 2D;\n"
"END\n"
"\n"},
{SR_VP,5,
"!!ARBvp1.0\n"
"TEMP texcoord;\n"
"\n"
"ADD texcoord.xy, vertex.position, {1}.x;\n"
"MOV texcoord.z, {0}.x;\n"
"MOV texcoord.w, {2}.x;\n"
"\n"
"MOV result.position, vertex.position;\n"
"MOV result.texcoord[0], texcoord;\n"
"END\n"
"\n"},
{SR_FP,17,
"!!ARBfp1.0\n"
"TXP result.color, fragment.texcoord[0], texture[0], 2D;\n"
"END\n"
"\n"},
{SR_VP,9,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"\n"
"ADDRESS\tA0;\n"
"PARAM\tp[2] = { program.local[0..1] };\n"
"\n"
"ARL\tA0.x, program.local[2].x;\n"
"MOV\tresult.color, p[A0.x];\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"ATTRIB texcoord = vertex.texcoord[0];\n"
"PARAM scale = {0.5, 0.5, 0.0, 1.0};\n"
"PARAM bias = {-0.25, -0.25, 0.0, 0.0};\n"
"MAD result.position, texcoord, scale, bias;\n"
"MOV result.color, vertex.color;\n"
"END\n"
"\n"},
{SR_VP,12,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"\n"
"TEMP tx, ty, tz, t;\n"
"TEMP tweight;\n"
"\n"
"MOV tweight, vertex.texcoord[0];\n"
"MUL tx, tweight.x, program.local[0];\n"
"MUL ty, tweight.x, program.local[1];\n"
"\n"
"MOV t, program.local[2];\n"
"MAD tx, tweight.y, program.local[2], tx;\n"
"MOV t, program.local[3];\n"
"MAD ty, tweight.y, t, ty;\n"
"\n"
"ADD result.color, tx, ty;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM zero = program.local[0];\n"
"MOV result.position, vertex.position;\n"
"RCP result.color.x, zero.x;\n"
"RCP result.color.y, zero.y;\n"
"RCP result.color.z, zero.z;\n"
"RCP result.color.w, zero.w;\n"
"END\n"
"\n"},
{SR_VP,11,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"DP3 result.fogcoord, vertex.texcoord[0], vertex.texcoord[1];\n"
"END\n"
"\n"},
{SR_FP,17,
"!!ARBfp1.0\n"
"MOV result.color, fragment.fogcoord;\n"
"END\n"
"\n"},
{SR_VP,11,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"DP4 result.fogcoord, vertex.texcoord[0], vertex.texcoord[1];\n"
"END\n"
"\n"},
{SR_FP,17,
"!!ARBfp1.0\n"
"MOV result.color, fragment.fogcoord;\n"
"END\n"
"\n"},
{SR_VP,11,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"DPH result.fogcoord, vertex.texcoord[0], vertex.texcoord[1];\n"
"END\n"
"\n"},
{SR_FP,17,
"!!ARBfp1.0\n"
"MOV result.color, fragment.fogcoord;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM zero = program.local[0];\n"
"PARAM infNan = program.local[9];\n"
"MOV result.position, vertex.position;\n"
"ADD result.color, infNan, zero;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM p2 = program.local[2];\n"
"MOV result.position, vertex.position;\n"
"ABS result.color, p2;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM p = program.local[1];\n"
"MOV result.position, vertex.position;\n"
"ADD result.color, vertex.color, p;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"ADDRESS addr;\n"
"PARAM indexes = {-1, 0, 1, 2};\n"
"PARAM myArray[4] = {{0.11, 0.12, 0.13, 0.14},\n"
"{0.21, 0.22, 0.23, 0.24},\n"
"{0.31, 0.32, 0.33, 0.34},\n"
"{0.41, 0.42, 0.43, 0.44}};\n"
"MOV result.position, vertex.position;\n"
"\n"
"# Load ARL with -1, get array[0].x\n"
"ARL addr.x, indexes.x;\n"
"MOV result.color.x, myArray[addr.x + 1];\n"
"\n"
"# Load ARL with 0, get array[1].y\n"
"ARL addr.x, indexes.y;\n"
"MOV result.color.y, myArray[addr.x + 1];\n"
"\n"
"# Load ARL with 1, get array[2].z\n"
"ARL addr.x, indexes.z;\n"
"MOV result.color.z, myArray[addr.x + 1];\n"
"\n"
"# Load ARL with 2, get array[3].w\n"
"ARL addr.x, indexes.w;\n"
"MOV result.color.w, myArray[addr.x + 1];\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM p2 = program.local[2];\n"
"PARAM bias = { 0.5, 0.5, 0.5, 0.5 };\n"
"TEMP t;\n"
"MOV result.position, vertex.position;\n"
"DP3 t, p2, vertex.color;\n"
"ADD result.color, t, bias;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM p2 = program.local[2];\n"
"PARAM bias = { 0.5, 0.5, 0.5, 0.5 };\n"
"TEMP t;\n"
"MOV result.position, vertex.position;\n"
"DP4 t, p2, vertex.color;\n"
"ADD result.color, t, bias;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM p2 = program.local[2];\n"
"TEMP t;\n"
"MOV result.position, vertex.position;\n"
"DPH result.color, p2, vertex.color;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"# let d = 0.4\n"
"PARAM v1 = {9.9, 0.16, 0.16, 9.9};\n"
"PARAM v2 = {9.9, 2.5, 9.9, 2.5};\n"
"MOV result.position, vertex.position;\n"
"DST result.color, v1, v2;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM scale = {0.01, 0.01, 0.01, 0.01};\n"
"PARAM values = {0.0, 1.0, 4.0, -2.0 };\n"
"TEMP t;\n"
"MOV result.position, vertex.position;\n"
"EX2 t.x, values.x;\n"
"EX2 t.y, values.y;\n"
"EX2 t.z, values.z;\n"
"EX2 t.w, values.w;\n"
"MUL result.color, t, scale;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM scale = {0.01, 0.01, 0.01, 0.01};\n"
"PARAM values = {4.5, 0, 0, 0};\n"
"TEMP t;\n"
"MOV result.position, vertex.position;\n"
"EXP t, values.x;\n"
"MUL result.color, t, scale;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM values = {4.8, 0.3, -0.2, 1.2};\n"
"PARAM scale = {0.1, 0.1, 0.1, 0.1};\n"
"MOV result.position, vertex.position;\n"
"TEMP t;\n"
"FLR t, values;\n"
"MUL result.color, t, scale;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM values = {1.344, -1.5, -10.1, 4.2};\n"
"MOV result.position, vertex.position;\n"
"FRC result.color, values;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM values = {64.0, 1, 30, 4};\n"
"PARAM scale = {0.1, 0.1, 0.1, 0.1};\n"
"MOV result.position, vertex.position;\n"
"TEMP t;\n"
"LG2 t.x, values.x;\n"
"LG2 t.y, values.y;\n"
"LG2 t.z, values.z;\n"
"LG2 t.w, values.w;\n"
"MUL result.color, t, scale;\n"
"END\n"
"\n"},
{SR_VP,7,
"!!ARBvp1.0\n"
"PARAM values = {0.65, 0.0, 0.0, 0.0};\n"
"MOV result.position, vertex.position;\n"
"LIT result.color, values;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM values = {-0.5, 0.0, 0.0, 0.0};\n"
"MOV result.position, vertex.position;\n"
"LIT result.color, values;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM values = {64.0, 50, 30, 4};\n"
"PARAM scale = {0.1, 0.1, 0.1, 0.1};\n"
"MOV result.position, vertex.position;\n"
"TEMP t;\n"
"LOG t.x, values.x;\n"
"LOG t.y, values.y;\n"
"LOG t.z, values.z;\n"
"LOG t.w, values.w;\n"
"MUL result.color, t, scale;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM p1 = program.local[1];\n"
"PARAM p2 = program.local[2];\n"
"MOV result.position, vertex.position;\n"
"MAD result.color, vertex.color, p1, p2;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM p1 = program.local[1];\n"
"PARAM p2 = program.local[2];\n"
"MOV result.position, vertex.position;\n"
"MAX result.color, p1, p2;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM p1 = program.local[1];\n"
"MOV result.position, vertex.position;\n"
"MIN result.color, p1, vertex.color;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"MOV result.position, vertex.position;\n"
"MOV result.color, vertex.color.wzxy;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM p1 = program.local[1];\n"
"MOV result.position, vertex.position;\n"
"MUL result.color.xy, p1.wzww, vertex.color.wzww;\n"
"MUL result.color.zw, p1.xxyx, vertex.color.xxyx;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM values = {0.5, 2, 3, 4};\n"
"MOV result.position, vertex.position;\n"
"POW result.color.x, values.x, values.y;\n"
"POW result.color.y, values.x, values.z;\n"
"POW result.color.z, values.x, values.w;\n"
"POW result.color.w, values.w, values.x;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM values = {8, -10, 1, 12 };\n"
"MOV result.position, vertex.position;\n"
"RCP result.color.x, values.x;\n"
"RCP result.color.y, values.y;\n"
"RCP result.color.z, values.z;\n"
"RCP result.color.w, values.w;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM values = {1, 4, 9, 100 };\n"
"MOV result.position, vertex.position;\n"
"RSQ result.color.x, values.x;\n"
"RSQ result.color.y, values.y;\n"
"RSQ result.color.z, values.z;\n"
"RSQ result.color.w, values.w;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM values = {-100, -5, -1, 0};\n"
"MOV result.position, vertex.position;\n"
"RSQ result.color.x, values.x;\n"
"RSQ result.color.y, values.y;\n"
"RSQ result.color.z, values.z;\n"
"RSQ result.color.w, values.w;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM p0 = program.local[0];\n"
"PARAM p2 = program.local[2];\n"
"MOV result.position, vertex.position;\n"
"SGE result.color, p2, p0;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM p0 = program.local[0];\n"
"PARAM p2 = program.local[2];\n"
"MOV result.position, vertex.position;\n"
"SLT result.color, p2, p0;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM p1 = program.local[1];\n"
"MOV result.position, vertex.position;\n"
"SUB result.color, p1.yxwz, vertex.color.primary.yxwz;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM p = program.local[1];\n"
"MOV result.position, vertex.position;\n"
"SWZ result.color, p, w,x,x,y;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM p = program.local[1];\n"
"MOV result.position, vertex.position;\n"
"SWZ result.color, p, 0,1,0,1;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM p = program.local[1];\n"
"MOV result.position, vertex.position;\n"
"SWZ result.color, p, -w,-x,x,y;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM p = program.local[1];\n"
"MOV result.position, vertex.position;\n"
"SWZ result.color, p, 1,x,z,0;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM p = program.local[1];\n"
"MOV result.position, vertex.position;\n"
"SWZ result.color, p, z,-y,-1,0;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM p1 = program.local[1];\n"
"PARAM p2 = program.local[2];\n"
"MOV result.position, vertex.position;\n"
"XPD result.color, p1, p2;\n"
"END\n"
"\n"},
{SR_VP,6,
"!!ARBvp1.0\n"
"PARAM p1 = program.local[1];\n"
"PARAM p2 = program.local[2];\n"
"TEMP t;\n"
"MOV result.position, vertex.position;\n"
"MOV t, p1;\n"
"XPD t, t, p2;\n"
"MOV result.color, t;\n"
"END\n"
"\n"},
{SR_VP,9,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"PARAM vals[] =\t{\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0}\n"
"\t\t};\n"
"ADDRESS A0;\n"
"\n"
"ARL A0.x, program.local[0].x;\n"
"MOV result.color, vals[A0.x-1];\n"
"END\n"
"\n"},
{SR_VP,8,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"PARAM vals[] =\t{\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0}\n"
"\t\t};\n"
"ADDRESS A0;\n"
"\n"
"ARL A0.x, program.local[0].x;\n"
"MOV result.color, vals[A0.x+1];\n"
"END\n"
"\n"},
{SR_VP,10,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"PARAM vals[] =\t{\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0}\n"
"\t\t};\n"
"ADDRESS A0;\n"
"\n"
"ARL A0.x, program.local[0].x;\n"
"# loading index 1 of the array\n"
"MOV result.color, vals[A0.x+0];\n"
"MOV result.color, vals[0];\n"
"END\n"
"\n"},
{SR_VP,8,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"PARAM vals[] =\t{\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0}\n"
"\t\t};\n"
"ADDRESS A0;\n"
"\n"
"ARL A0.x, program.local[0].x;\n"
"MOV result.color, vals[A0.x+109];\n"
"END\n"
"\n"},
{SR_VP,8,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"PARAM vals[] =\t{\n"
"\t\t\t{ 0.75,  0.25, 0.0, 0.0},\n"
"\t\t\t{-0.25,  0.25, 0.0, 0.0},\n"
"\t\t\t{ 0.75, -0.75, 0.0, 0.0},\n"
"\t\t\t{-0.25, -0.75, 0.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\n"
"\n"
"\n"
"\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 1.0, 0.0}\n"
"\t\t};\n"
"ADDRESS A0;\n"
"TEMP temp;\n"
"MUL temp.x, vertex.position.y, {2};\n"
"ADD temp.x, temp, vertex.position.x;\n"
"ARL A0.x, temp.x;\n"
"ADD result.color, vals[A0.x], vertex.position.xyzz;\n"
"END\n"
"\n"},
{SR_VP,10,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"PARAM vals[] =\t{\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0}\n"
"\t\t};\n"
"ADDRESS A0;\n"
"\n"
"ARL A0.x, program.local[0].x;\n"
"MOV result.color, vals[A0.x+0];\n"
"END\n"
"\n"},
{SR_VP,8,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"PARAM vals[] =\t{\n"
"\t\t\t{ 0.75,  0.25, 0.0, 0.0},\n"
"\t\t\t{-0.25,  0.25, 0.0, 0.0},\n"
"\t\t\t{ 0.75, -0.75, 0.0, 0.0},\n"
"\t\t\t{-0.25, -0.75, 0.0, 0.0}\n"
"\t\t};\n"
"ADDRESS A0;\n"
"TEMP temp;\n"
"MUL temp.x, vertex.position.y, {2};\n"
"ADD temp.x, temp, vertex.position.x;\n"
"ARL A0.x, temp.x;\n"
"ADD result.color, vals[A0.x], vertex.position.xyzz;\n"
"END\n"
"\n"},
{SR_VP,8,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"PARAM vals[4] =\t{\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0}\n"
"\t\t};\n"
"ADDRESS A0;\n"
"ARL A0.x, program.local[0].x;\n"
"MOV result.color, vals[A0.x+0];\n"
"END\n"
"\n"},
{SR_VP,8,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"PARAM vals[4] = { program.env[1..4] };\n"
"ADDRESS A0;\n"
"ARL A0.x, program.local[0].x;\n"
"MOV result.color, vals[A0.x+0];\n"
"END\n"
"\n"},
{SR_VP,8,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"PARAM vals[4] = { program.local[1..4] };\n"
"ADDRESS A0;\n"
"ARL A0.x, program.local[0].x;\n"
"MOV result.color, vals[A0.x+0];\n"
"END\n"
"\n"},
{SR_VP,9,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"PARAM vals[4] =\t{\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0}\n"
"\t\t};\n"
"ADDRESS A0;\n"
"ARL A0.x, program.local[0].x;\n"
"MOV result.color, vals[A0.x+6];\n"
"END\n"
"\n"},
{SR_VP,9,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"PARAM vals[4] =\t{\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0}\n"
"\t\t};\n"
"ADDRESS A0;\n"
"ARL A0.x, program.local[0].x;\n"
"MOV result.color, vals[A0.x-3];\n"
"END\n"
"\n"},
{SR_VP,10,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"PARAM vals[] =\t{\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0},\n"
"\t\t\t{0.5, 1.0, 2.0, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.8, 0.0},\n"
"\t\t\t{1.0, 0.0, 0.5, 0.0},\n"
"\t\t\t{0.8, 1.0, 0.0, 0.0}\n"
"\t\t};\n"
"\n"
"MOV result.color, vals[3];\n"
"END\n"
"\n"},
{SR_VP,8,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"PARAM vals[4] = { {0.5, 1.0, 2.0, 0.0},\n"
"      \t      \t  {1.0, 0.0, 0.8, 0.0},\n"
"\t\t  {1.0, 0.0, 0.5, 0.0},\n"
"\t\t  {0.8, 1.0, 0.0, 0.0}\n"
"\t\t};\n"
"MOV result.color, vals[3];\n"
"END\n"
"\n"},
{SR_VP,8,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"\n"
"TEMP temp1, temp2;\n"
"MOV temp1, 2.0;\n"
"MOV temp2, -2.0;\n"
"ADD temp1, temp1, temp2;\n"
"ABS temp1, temp1;\n"
"SLT temp1, -temp1, {0.0};\n"
"MOV result.color, {0.0, 1.0, 0.0, 0.0};\n"
"MOV result.color.x, temp1;\n"
"END\n"
"\n"},
{SR_VP,9,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"TEMP temp1;\n"
"\n"
"MOV temp1, {1, 2.5, 2, 0};\n"
"EXP temp1, temp1.y;\n"
"# EXP result should be {4, .5, 5.65, 1.0};\n"
"ADD result.color, temp1, {-3.5, 0, -5.15, -0.5};\n"
"END\n"
"\n"},
{SR_VP,9,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"\n"
"TEMP temp1, temp2;\n"
"\n"
"MOV temp1, {0.0,  0.25, 2.0, -1.0};\n"
"MOV temp2, {0.25, 0.0, -1.0, 2.0};\n"
"MAX temp1, temp1, temp2;\n"
"ADD result.color, temp1, {0.0, 0.0, -1.75, -1.75};\n"
"END\n"
"\n"},
{SR_VP,9,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"\n"
"TEMP temp1, temp2;\n"
"\n"
"MOV temp1, {1.0, 0.25, 2.0, -1.0};\n"
"MOV temp2, {0.25, 1.0, -1.0, 2.0};\n"
"MIN temp1, temp1, temp2;\n"
"ADD result.color, temp1, {0.0, 0.0, 1.25, 1.25};\n"
"END\n"
"\n"},
{SR_VP,9,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"TEMP temp;\n"
"MOV temp, {3, 0, 1, 0};\n"
"SGE temp, temp, {2, 1, 0, -1};\n"
"MOV result.color, temp;\n"
"END\n"
"\n"},
{SR_VP,12,
"!!ARBvp1.0\n"
"OPTION ARB_position_invariant;\n"
"ADD result.color, {1, 0, .25, 0}, {0.5, 0, 1, 0};\n"
"END\n"
"\n"},
{SR_VP,7,
"!!ARBvp1.0\n"
"PARAM p1 = program.local[1];\n"
"MOV result.position, vertex.position;\n"
"MOV result.position.z, p1.y;\n"
"MOV result.color, vertex.color;\n"
"END\n"
"\n"},
{SR_FS,5,
"void main()\n"
"{\n"
"\tfloat x = 0.75;\n"
"\t// this should always be false\n"
"\tif (x <= 0.5 && ++x > 0.0) {\n"
"\t\tx += 0.1;\n"
"\t}\n"
"\tgl_FragColor = vec4(x);\n"
"}\n"
"\n"
"\n"},
{SR_VS,5,
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"\t// front color values should all be >= 1.0\n"
"\tgl_FrontColor = vec4(gl_MaxLights, gl_MaxClipPlanes,\n"
"\t\t  \t\tgl_MaxTextureUnits,\n"
"\t\t  \t\tgl_MaxTextureCoords);\n"
"}\n"
"\n"
"\n"},
{SR_FS,16,
"void main()\n"
"{\n"
"\tgl_FragColor = gl_Color;\n"
"}\n"
"\n"},
{SR_FS,5,
"void main()\n"
"{\n"
"\tfloat x, y, z;\n"
"\tx = y = z = 0.25;\n"
"\tgl_FragColor = vec4(x + y + z);\n"
"}\n"
"\n"
"\n"},
{SR_VS,7,
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,13,
"uniform float one;\n"
"uniform float zero;\n"
"uniform bool f;\n"
"\n"
"void main()\n"
"{\n"
"\tif (one < zero == f)\n"
"\t\tgl_FragColor = vec4(0.0, 1.0, 0.0, 0.0);\n"
"\telse\n"
"\t\tgl_FragColor = vec4(1.0, 0.0, 0.0, 0.0);\n"
"}\n"
"\n"},
{SR_VS,7,
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,13,
"uniform float one;\n"
"uniform float zero;\n"
"uniform bool t;\n"
"\n"
"void main()\n"
"{\n"
"\tif (zero < one == t)\n"
"\t\tgl_FragColor = vec4(0.0, 1.0, 0.0, 0.0);\n"
"\telse\n"
"\t\tgl_FragColor = vec4(1.0, 0.0, 0.0, 0.0);\n"
"}\n"
"\n"},
{SR_VS,17,
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,23,
"void main()\n"
"{\n"
"\tfloat x = gl_FragCoord.x;\n"
"\tfloat y = gl_FragCoord.y;\n"
"\tfloat xy = x * y;\n"
"\tfloat dxydx = dFdx(xy);\n"
"\tif (distance(dxydx, y) > 1.0e-3)\n"
"\t\tgl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n"
"\telse\n"
"\t\tgl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_VS,17,
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,23,
"void main()\n"
"{\n"
"\tfloat x = gl_FragCoord.x;\n"
"\tfloat y = gl_FragCoord.y;\n"
"\tfloat xy = x * y;\n"
"\tfloat dxydy = dFdy(xy);\n"
"\tif (distance(dxydy, x) > 1.0e-3)\n"
"\t\tgl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n"
"\telse\n"
"\t\tgl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_PASSVS,9,
"\n"},
{SR_FS,11,
"uniform bool use_discard;\n"
"uniform float boundary;\n"
"uniform vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"\tif (use_discard) {\n"
"\t\tfloat tmp;\n"
"\t\ttmp = boundary * 0.5;\n"
"\t\tif (gl_FragCoord.x < tmp)\n"
"\t\t\tdiscard;\n"
"\t}\n"
"\n"
"\tgl_FragColor = color;\n"
"}\n"
"\n"},
{SR_VS,5,
"varying vec4 c;\n"
"attribute vec4 piglit_vertex;\n"
"\n"
"void main()\n"
"{\n"
"    c = vec4(2.0, 13.0, 1.0, 0.0);\n"
"    gl_Position = piglit_vertex;\n"
"}\n"
"\n"},
{SR_FS,15,
"uniform vec4 a;\n"
"uniform float b;\n"
"\n"
"varying vec4 c;\n"
"\n"
"void main()\n"
"{\n"
"    /* This tickles a bug in the i965 driver on GEN4 and GEN5 platforms.  An\n"
"     * optimization tries to be clever doing a float cast of comparison with a\n"
"     * constant value of zero.  However, the generated comparison was\n"
"     * inverted, so everything went wrong.\n"
"     */\n"
"    gl_FragColor = vec4(equal(a * c, vec4(b, 13, 0, 0)));\n"
"}\n"
"\n"},
{SR_PASSVS,5,
"\n"},
{SR_FS,7,
"void main()\n"
"{\n"
"\tfloat f = gl_FrontFacing ? 0.0 : -1.0;\n"
"\tf = f * 0.5 + 0.75; // Remap (0.0, -1.0) -> (0.75, 0.25)\n"
"\tgl_FragColor = vec4(0.0, f, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_PASSVS,5,
"\n"},
{SR_FS,7,
"uniform int front;\n"
"uniform int back;\n"
"\n"
"const vec4 c = vec4(1.0, 0.0, 0.0, 0.0);\n"
"void main()\n"
"{\n"
"\tint f = gl_FrontFacing ? 1 : -1;\n"
"\tif (f == front) {\n"
"\t\tgl_FragColor = c.yxyx;\n"
"\t} else if (f == back) {\n"
"\t\tgl_FragColor = c.yyxx;\n"
"\t} else {\n"
"\t\tgl_FragColor = c.xyyx;\n"
"\t}\n"
"}\n"
"\n"},
{SR_PASSVS,5,
"\n"},
{SR_FS,7,
"void main()\n"
"{\n"
"\tfloat f = gl_FrontFacing ? 1.0 : -1.0;\n"
"\tf = f * 0.25 + 0.5; // Remap (1.0, -1.0) -> (0.75, 0.25)\n"
"\tgl_FragColor = vec4(0.0, f, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_PASSVS,5,
"\n"},
{SR_FS,7,
"void main()\n"
"{\n"
"\tfloat f = gl_FrontFacing ? -1.0 : 1.0;\n"
"\tf = f * -0.25 + 0.5; // Remap (-1.0, 1.0) -> (0.75, 0.25)\n"
"\tgl_FragColor = vec4(0.0, f, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_PASSVS,9,
"\n"},
{SR_FS,11,
"void main()\n"
"{\n"
"\tvec4 foo;\n"
"\tif (gl_FrontFacing)\n"
"\t\tfoo = vec4(-1.0);\n"
"\telse\n"
"\t\tfoo = vec4(1.0);\n"
"\n"
"\tgl_FragColor = vec4(1.5 + foo);\n"
"}\n"
"\n"},
{SR_VS,8,
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,14,
"void main()\n"
"{\n"
"  int sum;\n"
"  int i;\n"
"  bool failed = false;\n"
"\n"
"  sum = 0; for (i = 10; i < 14; i++) sum += i; if (sum != 46) failed = true;\n"
"  sum = 0; for (i = 10; i <= 13; i++) sum += i; if (sum != 46) failed = true;\n"
"  sum = 0; for (i = 10; 14 > i; i++) sum += i; if (sum != 46) failed = true;\n"
"  sum = 0; for (i = 10; 13 >= i; i++) sum += i; if (sum != 46) failed = true;\n"
"  sum = 0; for (i = 10; i != 14; i++) sum += i; if (sum != 46) failed = true;\n"
"  sum = 0; for (i = 10; 14 != i; i++) sum += i; if (sum != 46) failed = true;\n"
"  sum = 0; for (i = 10; i == 10; i++) sum += i; if (sum != 10) failed = true;\n"
"  sum = 0; for (i = 10; 10 == i; i++) sum += i; if (sum != 10) failed = true;\n"
"\n"
"  sum = 0; for (i = 13; i > 9; i--) sum += i; if (sum != 46) failed = true;\n"
"  sum = 0; for (i = 13; i >= 10; i--) sum += i; if (sum != 46) failed = true;\n"
"  sum = 0; for (i = 13; 9 < i; i--) sum += i; if (sum != 46) failed = true;\n"
"  sum = 0; for (i = 13; 10 <= i; i--) sum += i; if (sum != 46) failed = true;\n"
"  sum = 0; for (i = 13; i != 9; i--) sum += i; if (sum != 46) failed = true;\n"
"  sum = 0; for (i = 13; 9 != i; i--) sum += i; if (sum != 46) failed = true;\n"
"  sum = 0; for (i = 13; i == 13; i--) sum += i; if (sum != 13) failed = true;\n"
"  sum = 0; for (i = 13; 13 == i; i--) sum += i; if (sum != 13) failed = true;\n"
"\n"
"  gl_FragColor = failed ? vec4(1.0, 0.0, 0.0, 1.0) : vec4(0.0, 1.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_PASSVS,8,
"\n"},
{SR_FS,10,
"void main()\n"
"{\n"
"  gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);\n"
"  while (true) {\n"
"    return;\n"
"  }\n"
"}\n"
"\n"},
{SR_PASSVS,5,
"\n"},
{SR_FS,7,
"\n"
"uniform float u;\n"
"uniform float u2;\n"
"\n"
"void main()\n"
"{\n"
"  float a = u;\n"
"  float b = u2;\n"
"\n"
"  a = max(a, 0.35);\n"
"  b = max(b, 0.35);\n"
"\n"
"  float c = max(a, b);\n"
"\n"
"  gl_FragColor = vec4(0.0, c, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_PASSVS,5,
"\n"},
{SR_FS,7,
"\n"
"uniform float u;\n"
"uniform float u2;\n"
"\n"
"void main()\n"
"{\n"
"  float a = u;\n"
"  float b = u2;\n"
"\n"
"  a = min(a, 0.35);\n"
"  b = min(b, 0.35);\n"
"\n"
"  float c = min(a, b);\n"
"\n"
"  gl_FragColor = vec4(0.0, c, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_VS,11,
"void main(void)\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,17,
"uniform int a;\n"
"\n"
"vec4 getColour() {\n"
"  /* We use a uniform in the outer loop comparision so it's not immediately\n"
"   * unrolled.\n"
"   */\n"
"  for(int v124 = 0; v124 != a; v124++) {\n"
"    for(int v229 = 1; v229 > 0; v229--) {\n"
"      return vec4(0.0, 1.0, 0.0, 1.0);\n"
"    }\n"
"  }\n"
"\n"
"  return vec4(1.0, 0.0, 0.0, 1.0);\n"
"}\n"
"\n"
"void main(void) {\n"
"  gl_FragColor = getColour();\n"
"}\n"
"\n"},
{SR_PASSVS,8,
"\n"},
{SR_FS,10,
"uniform int a;\n"
"\n"
"vec4 func1()\n"
"{\n"
"   if(a >= 1) {\n"
"      for(int i = 0; i < a; i++) {\n"
"         return vec4(0.0, 1.0, 0.0, 1.0);\n"
"      }\n"
"   }\n"
"   return vec4(1.0, 0.0, 0.0, 1.0);\n"
"}\n"
"\n"
"void main(void)\n"
"{\n"
"    gl_FragColor = func1();\n"
"}\n"
"\n"},
{SR_VS,8,
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,14,
"bvec4 foo(vec4 x, vec4 y)\n"
"{\n"
"  return notEqual(x, y);\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"  gl_FragColor = vec4(foo(vec4(0.0, 0.0, 1.0, 1.0),\n"
"                          vec4(0.0, 1.0, 1.0, 0.0)));\n"
"}\n"
"\n"},
{SR_VS,5,
"varying vec4 c;\n"
"attribute vec4 piglit_vertex;\n"
"\n"
"void main()\n"
"{\n"
"    c = vec4(38.2, 2.0, 0.0, 1.0);\n"
"    gl_Position = piglit_vertex;\n"
"}\n"
"\n"},
{SR_FS,15,
"uniform vec4 a;\n"
"uniform float b;\n"
"\n"
"varying vec4 c;\n"
"\n"
"void main()\n"
"{\n"
"    /* This tickles a bug in the i965 driver on GEN4 and GEN5 platforms.  An\n"
"     * optimization tries to be clever doing a float cast of comparison with a\n"
"     * constant value of zero.  However, the generated comparison was\n"
"     * inverted, so everything went wrong.\n"
"     */\n"
"    gl_FragColor = vec4(notEqual(a * c, vec4(b, 13, 0, 0)));\n"
"}\n"
"\n"},
{SR_PASSVS,5,
"\n"},
{SR_FS,7,
"uniform ivec2 arg0;\n"
"uniform ivec2 arg1;\n"
"uniform ivec2 expected;\n"
"\n"
"void main()\n"
"{\n"
"  ivec2 result = arg0 * arg1;\n"
"  result = result.xx * ivec2(2, 3);\n"
"  gl_FragColor = result == expected ? vec4(0.0, 1.0, 0.0, 1.0) : vec4(1.0, 0.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_VS,5,
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,11,
"uniform vec4 v;\n"
"\n"
"void main()\n"
"{\n"
"\tgl_FragColor = 0.25 + clamp(exp2(v), 0.0, 1.0) * 0.5;\n"
"}\n"
"\n"},
{SR_VS,5,
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,11,
"uniform vec4 x;\n"
"uniform vec4 y;\n"
"\n"
"void main()\n"
"{\n"
"\tgl_FragColor = 0.25 + clamp(pow(x, y), 0.0, 1.0) * 0.5;\n"
"}\n"
"\n"},
{SR_VS,5,
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,11,
"uniform vec4 v;\n"
"\n"
"void main()\n"
"{\n"
"\tgl_FragColor = 0.25 + clamp(sqrt(v), 0.0, 1.0) * 0.5;\n"
"}\n"
"\n"},
{SR_PASSVS,5,
"\n"},
{SR_FS,7,
"uniform float a;\n"
"uniform float b;\n"
"uniform vec4 color0;\n"
"uniform vec4 color1;\n"
"\n"
"void main()\n"
"{\n"
"    /* This is a general step() test, but it is designed to tickle an\n"
"     * optimization path in the GEN4 and GEN5 code generation in the i965\n"
"     * driver.  This optimization tries to generate different code for\n"
"     * expressions like 'float(expr cmp 0)'.\n"
"     */\n"
"    gl_FragColor = step(0.0, a * b) * color0 + color1;\n"
"}\n"
"\n"},
{SR_PASSVS,12,
"\n"},
{SR_FS,14,
"uniform sampler2D tex0, tex1;\n"
"\n"
"void main()\n"
"{\n"
"    bool pat = fract(gl_FragCoord.x / 2.0) < 0.5;\n"
"    gl_FragColor = pat ?\n"
"             texture2D(tex0, gl_TexCoord[0].xy) :\n"
"             texture2D(tex1, gl_TexCoord[0].xy);\n"
"\n"
"    if (pat)\n"
"        gl_FragColor = vec4(1.0) - gl_FragColor;\n"
"}\n"
"\n"},
{SR_VS,8,
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,14,
"#define RED vec4(1.0, 0.0, 0.0, 0.0)\n"
"#define GREEN vec4(0.0, 1.0, 0.0, 0.0)\n"
"\n"
"uniform int three;\n"
"uniform float five;\n"
"\n"
"void main()\n"
"{\n"
"  vec4 v = vec4(1.0, 2.0, 3.0, 4.0);\n"
"\n"
"  // If the bug is present, constant propagation will see a scalar on\n"
"  // the RHS, and incorrectly conclude that therefore only v.x is\n"
"  // killed.\n"
"  v[three] = five;\n"
"\n"
"  vec4 color = v.a == 5.0 ? GREEN : RED;\n"
"\n"
"  // If the bug is present, then index and new_value will be optimized\n"
"  // away, making shader_runner fail to assign uniforms.  To avoid\n"
"  // that, multiply color by (five - three - 1.0).\n"
"  gl_FragColor = color * (five - float(three) - 1.0);\n"
"}\n"
"\n"},
{SR_VS,10,
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,16,
"float f01(in float f[4]);\n"
"float f02(in float f[4]);\n"
"float f06(in float f[4]);\n"
"float f10(in float f[4]);\n"
"float f14(in float f[4]);\n"
"void main()\n"
"{\n"
"\tfloat g[4];\n"
"\n"
"\tg[0] = 0.0;\n"
"\tg[1] = 1.0;\n"
"\tg[2] = 0.0;\n"
"\tg[3] = 0.1;\n"
"\n"
"\tf01(g);\n"
"\tgl_FragColor = vec4(f01(g), g[0], g[2], g[1]);\n"
"}\n"
"float f01(in float f[4]) {return f02(f);}\n"
"float f05(in float f[4]) {return f06(f);}\n"
"float f09(in float f[4]) {return f10(f);}\n"
"float f13(in float f[4]) {return f14(f);}\n"
"\n"},
{SR_FS,39,
"float f03(in float f[4]);\n"
"float f07(in float f[4]);\n"
"float f11(in float f[4]);\n"
"float f15(in float f[4]);\n"
"float f02(in float f[4]) {return f03(f);}\n"
"float f06(in float f[4]) {return f07(f);}\n"
"float f10(in float f[4]) {return f11(f);}\n"
"float f14(in float f[4]) {return f15(f);}\n"
"\n"},
{SR_FS,49,
"float f04(in float f[4]);\n"
"float f08(in float f[4]);\n"
"float f12(in float f[4]);\n"
"float f16(in float f[4]);\n"
"float f03(in float f[4]) {return f04(f);}\n"
"float f07(in float f[4]) {return f08(f);}\n"
"float f11(in float f[4]) {return f12(f);}\n"
"float f15(in float f[4]) {return f16(f);}\n"
"\n"},
{SR_FS,59,
"uniform int i;\n"
"\n"
"float f05(in float f[4]);\n"
"float f09(in float f[4]);\n"
"float f13(in float f[4]);\n"
"float f04(in float f[4]) {return f05(f);}\n"
"float f08(in float f[4]) {return f09(f);}\n"
"float f12(in float f[4]) {return f13(f);}\n"
"float f16(in float f[4]) {return f[i] + 0.1;}\n"
"\n"
"\n"},
{SR_VS,10,
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,16,
"float f01(in float f[4]);\n"
"float f02(in float f[4]);\n"
"float f06(in float f[4]);\n"
"float f10(in float f[4]);\n"
"float f14(in float f[4]);\n"
"void main()\n"
"{\n"
"\tfloat g[4];\n"
"\n"
"\tg[0] = 0.0;\n"
"\tg[1] = 1.0;\n"
"\tg[2] = 0.0;\n"
"\tg[3] = 0.1;\n"
"\n"
"\tf01(g);\n"
"\tgl_FragColor = vec4(f01(g), g[0], g[2], g[1]);\n"
"}\n"
"float f01(in float f[4]) {return f02(f);}\n"
"float f05(in float f[4]) {return f06(f);}\n"
"float f09(in float f[4]) {return f10(f);}\n"
"float f13(in float f[4]) {return f14(f);}\n"
"\n"},
{SR_FS,39,
"float f03(in float f[4]);\n"
"float f07(in float f[4]);\n"
"float f11(in float f[4]);\n"
"float f15(in float f[4]);\n"
"float f02(in float f[4]) {return f03(f);}\n"
"float f06(in float f[4]) {return f07(f);}\n"
"float f10(in float f[4]) {return f11(f);}\n"
"float f14(in float f[4]) {return f15(f);}\n"
"\n"},
{SR_FS,49,
"uniform int i;\n"
"\n"
"float f04(in float f[4]);\n"
"float f08(in float f[4]);\n"
"float f12(in float f[4]);\n"
"float f16(in float f);\n"
"float f03(in float f[4]) {return f04(f);}\n"
"float f07(in float f[4]) {return f08(f);}\n"
"float f11(in float f[4]) {return f12(f);}\n"
"float f15(in float f[4]) {return f16(f[i]);}\n"
"\n"},
{SR_FS,61,
"float f05(in float f[4]);\n"
"float f09(in float f[4]);\n"
"float f13(in float f[4]);\n"
"float f04(in float f[4]) {return f05(f);}\n"
"float f08(in float f[4]) {return f09(f);}\n"
"float f12(in float f[4]) {return f13(f);}\n"
"float f16(in float f) {return f + 0.1;}\n"
"\n"
"\n"},
{SR_VS,10,
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,16,
"float f01(in float f[4]);\n"
"float f02(in float f[4]);\n"
"float f06(in float f[4]);\n"
"float f10(in float f[4]);\n"
"float f14(in float f[4]);\n"
"void main()\n"
"{\n"
"\tfloat g[4];\n"
"\n"
"\tg[0] = 0.0;\n"
"\tg[1] = 1.0;\n"
"\tg[2] = 0.0;\n"
"\tg[3] = 0.1;\n"
"\n"
"\tf01(g);\n"
"\tgl_FragColor = vec4(f01(g), g[0], g[2], g[1]);\n"
"}\n"
"float f01(in float f[4]) {return f02(f);}\n"
"float f05(in float f[4]) {return f06(f);}\n"
"float f09(in float f[4]) {return f10(f);}\n"
"float f13(in float f[4]) {return f14(f);}\n"
"\n"},
{SR_FS,39,
"float f03(in float f[4]);\n"
"float f07(in float f[4]);\n"
"float f11(in float f[4]);\n"
"float f15(in float f[4]);\n"
"float f02(in float f[4]) {return f03(f);}\n"
"float f06(in float f[4]) {return f07(f);}\n"
"float f10(in float f[4]) {return f11(f);}\n"
"float f14(in float f[4]) {return f15(f);}\n"
"\n"},
{SR_FS,49,
"float f04(in float f[4]);\n"
"float f08(in float f[4]);\n"
"float f12(in float f[4]);\n"
"float f16(in float f[4]);\n"
"float f03(in float f[4]) {return f04(f);}\n"
"float f07(in float f[4]) {return f08(f);}\n"
"float f11(in float f[4]) {return f12(f);}\n"
"float f15(in float f[4]) {return f16(f);}\n"
"\n"},
{SR_FS,59,
"float f05(in float f[4]);\n"
"float f09(in float f[4]);\n"
"float f13(in float f[4]);\n"
"float f04(in float f[4]) {return f05(f);}\n"
"float f08(in float f[4]) {return f09(f);}\n"
"float f12(in float f[4]) {return f13(f);}\n"
"float f16(in float f[4]) {return f[3] + 0.1;}\n"
"\n"
"\n"},
{SR_VS,10,
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,16,
"float f01(in float f[4]);\n"
"float f02(in float f[4]);\n"
"float f06(in float f[4]);\n"
"float f10(in float f[4]);\n"
"float f14(in float f[4]);\n"
"void main()\n"
"{\n"
"\tfloat g[4];\n"
"\n"
"\tg[0] = 0.0;\n"
"\tg[1] = 1.0;\n"
"\tg[2] = 0.0;\n"
"\tg[3] = 0.1;\n"
"\n"
"\tf01(g);\n"
"\tgl_FragColor = vec4(f01(g), g[0], g[2], g[1]);\n"
"}\n"
"float f01(in float f[4]) {return f02(f);}\n"
"float f05(in float f[4]) {return f06(f);}\n"
"float f09(in float f[4]) {return f10(f);}\n"
"float f13(in float f[4]) {return f14(f);}\n"
"\n"},
{SR_FS,39,
"float f03(in float f[4]);\n"
"float f07(in float f[4]);\n"
"float f11(in float f[4]);\n"
"float f15(in float f[4]);\n"
"float f02(in float f[4]) {return f03(f);}\n"
"float f06(in float f[4]) {return f07(f);}\n"
"float f10(in float f[4]) {return f11(f);}\n"
"float f14(in float f[4]) {return f15(f);}\n"
"\n"},
{SR_FS,49,
"float f04(in float f[4]);\n"
"float f08(in float f[4]);\n"
"float f12(in float f[4]);\n"
"float f16(in float f);\n"
"float f03(in float f[4]) {return f04(f);}\n"
"float f07(in float f[4]) {return f08(f);}\n"
"float f11(in float f[4]) {return f12(f);}\n"
"float f15(in float f[4]) {return f16(f[3]);}\n"
"\n"},
{SR_FS,59,
"float f05(in float f[4]);\n"
"float f09(in float f[4]);\n"
"float f13(in float f[4]);\n"
"float f04(in float f[4]) {return f05(f);}\n"
"float f08(in float f[4]) {return f09(f);}\n"
"float f12(in float f[4]) {return f13(f);}\n"
"float f16(in float f) {return f + 0.1;}\n"
"\n"
"\n"},
{SR_VS,10,
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,16,
"float f01(in vec4 f);\n"
"float f02(in vec4 f);\n"
"float f06(in vec4 f);\n"
"float f10(in vec4 f);\n"
"float f14(in vec4 f);\n"
"void main()\n"
"{\n"
"\tvec4 g = vec4(0, 0.1, 0, 1.0);\n"
"\tf01(g);\n"
"\tgl_FragColor = vec4(f01(g), g.x, g.z, g.w);\n"
"}\n"
"float f01(in vec4 f) {return f02(f);}\n"
"float f05(in vec4 f) {return f06(f);}\n"
"float f09(in vec4 f) {return f10(f);}\n"
"float f13(in vec4 f) {return f14(f);}\n"
"\n"},
{SR_FS,33,
"float f03(in vec4 f);\n"
"float f07(in vec4 f);\n"
"float f11(in vec4 f);\n"
"float f15(in vec4 f);\n"
"float f02(in vec4 f) {return f03(f);}\n"
"float f06(in vec4 f) {return f07(f);}\n"
"float f10(in vec4 f) {return f11(f);}\n"
"float f14(in vec4 f) {return f15(f);}\n"
"\n"},
{SR_FS,43,
"float f04(in vec4 f);\n"
"float f08(in vec4 f);\n"
"float f12(in vec4 f);\n"
"float f16(in float f);\n"
"float f03(in vec4 f) {return f04(f);}\n"
"float f07(in vec4 f) {return f08(f);}\n"
"float f11(in vec4 f) {return f12(f);}\n"
"float f15(in vec4 f) {return f16(f.y);}\n"
"\n"},
{SR_FS,53,
"float f05(in vec4 f);\n"
"float f09(in vec4 f);\n"
"float f13(in vec4 f);\n"
"float f04(in vec4 f) {return f05(f);}\n"
"float f08(in vec4 f) {return f09(f);}\n"
"float f12(in vec4 f) {return f13(f);}\n"
"float f16(in float f) {return f + 0.1;}\n"
"\n"
"\n"},
{SR_VS,10,
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,16,
"float f01(in vec4 f);\n"
"float f02(in vec4 f);\n"
"float f06(in vec4 f);\n"
"float f10(in vec4 f);\n"
"float f14(in vec4 f);\n"
"void main()\n"
"{\n"
"\tvec4 g = vec4(0, 0.1, 0, 1.0);\n"
"\tf01(g);\n"
"\tgl_FragColor = vec4(f01(g), g.x, g.z, g.w);\n"
"}\n"
"float f01(in vec4 f) {return f02(f);}\n"
"float f05(in vec4 f) {return f06(f);}\n"
"float f09(in vec4 f) {return f10(f);}\n"
"float f13(in vec4 f) {return f14(f);}\n"
"\n"},
{SR_FS,33,
"float f03(in vec4 f);\n"
"float f07(in vec4 f);\n"
"float f11(in vec4 f);\n"
"float f15(in vec4 f);\n"
"float f02(in vec4 f) {return f03(f);}\n"
"float f06(in vec4 f) {return f07(f);}\n"
"float f10(in vec4 f) {return f11(f);}\n"
"float f14(in vec4 f) {return f15(f);}\n"
"\n"},
{SR_FS,43,
"float f04(in vec4 f);\n"
"float f08(in vec4 f);\n"
"float f12(in vec4 f);\n"
"float f16(in vec4 f);\n"
"float f03(in vec4 f) {return f04(f);}\n"
"float f07(in vec4 f) {return f08(f);}\n"
"float f11(in vec4 f) {return f12(f);}\n"
"float f15(in vec4 f) {return f16(f);}\n"
"\n"},
{SR_FS,53,
"float f05(in vec4 f);\n"
"float f09(in vec4 f);\n"
"float f13(in vec4 f);\n"
"float f04(in vec4 f) {return f05(f);}\n"
"float f08(in vec4 f) {return f09(f);}\n"
"float f12(in vec4 f) {return f13(f);}\n"
"float f16(in vec4 f) {return f.y + 0.1;}\n"
"\n"
"\n"},
{SR_VS,10,
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,16,
"void f01(inout vec4 f);\n"
"void f02(inout vec4 f);\n"
"void f06(inout vec4 f);\n"
"void f10(inout vec4 f);\n"
"void f14(inout vec4 f);\n"
"void main()\n"
"{\n"
"\tvec4 g = vec4(0.1, 0, 0, 1.0);\n"
"\tf01(g);\n"
"\tgl_FragColor = g;\n"
"}\n"
"void f01(inout vec4 f) {f02(f);}\n"
"void f05(inout vec4 f) {f06(f);}\n"
"void f09(inout vec4 f) {f10(f);}\n"
"void f13(inout vec4 f) {f14(f);}\n"
"\n"},
{SR_FS,33,
"void f03(inout vec4 f);\n"
"void f07(inout vec4 f);\n"
"void f11(inout vec4 f);\n"
"void f15(inout vec4 f);\n"
"void f02(inout vec4 f) {f03(f);}\n"
"void f06(inout vec4 f) {f07(f);}\n"
"void f10(inout vec4 f) {f11(f);}\n"
"void f14(inout vec4 f) {f15(f);}\n"
"\n"},
{SR_FS,43,
"void f04(inout vec4 f);\n"
"void f08(inout vec4 f);\n"
"void f12(inout vec4 f);\n"
"void f16(inout float f);\n"
"void f03(inout vec4 f) {f04(f);}\n"
"void f07(inout vec4 f) {f08(f);}\n"
"void f11(inout vec4 f) {f12(f);}\n"
"void f15(inout vec4 f) {f16(f.x);}\n"
"\n"},
{SR_FS,53,
"void f05(inout vec4 f);\n"
"void f09(inout vec4 f);\n"
"void f13(inout vec4 f);\n"
"void f04(inout vec4 f) {f05(f);}\n"
"void f08(inout vec4 f) {f09(f);}\n"
"void f12(inout vec4 f) {f13(f);}\n"
"void f16(inout float f) {f+=0.1;}\n"
"\n"
"\n"},
{SR_VS,6,
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,12,
"void f01(inout float f);\n"
"void f02(inout float f);\n"
"void f06(inout float f);\n"
"void f10(inout float f);\n"
"void f14(inout float f);\n"
"void main()\n"
"{\n"
"\tfloat g = 0.1;\n"
"\tf01(g);\n"
"\tgl_FragColor = vec4(g, 0, 0, 1.0);\n"
"}\n"
"void f01(inout float f) {f02(f);}\n"
"void f05(inout float f) {f06(f);}\n"
"void f09(inout float f) {f10(f);}\n"
"void f13(inout float f) {f14(f);}\n"
"\n"},
{SR_FS,29,
"void f03(inout float f);\n"
"void f07(inout float f);\n"
"void f11(inout float f);\n"
"void f15(inout float f);\n"
"void f02(inout float f) {f03(f);}\n"
"void f06(inout float f) {f07(f);}\n"
"void f10(inout float f) {f11(f);}\n"
"void f14(inout float f) {f15(f);}\n"
"\n"},
{SR_FS,39,
"void f04(inout float f);\n"
"void f08(inout float f);\n"
"void f12(inout float f);\n"
"void f16(inout float f);\n"
"void f03(inout float f) {f04(f);}\n"
"void f07(inout float f) {f08(f);}\n"
"void f11(inout float f) {f12(f);}\n"
"void f15(inout float f) {f16(f);}\n"
"\n"},
{SR_FS,49,
"void f05(inout float f);\n"
"void f09(inout float f);\n"
"void f13(inout float f);\n"
"void f04(inout float f) {f05(f);}\n"
"void f08(inout float f) {f09(f);}\n"
"void f12(inout float f) {f13(f);}\n"
"void f16(inout float f) {f+=0.1;}\n"
"\n"
"\n"},
{SR_VS,10,
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,16,
"void f01(inout vec4 f);\n"
"void f02(inout vec4 f);\n"
"void f06(inout vec4 f);\n"
"void f10(inout vec4 f);\n"
"void f14(inout vec4 f);\n"
"void main()\n"
"{\n"
"\tvec4 g = vec4(0.0, 0.3, 0, 1.0);\n"
"\tf01(g);\n"
"\tgl_FragColor = g;\n"
"}\n"
"void f01(inout vec4 f) {f02(f);}\n"
"void f05(inout vec4 f) {f06(f);}\n"
"void f09(inout vec4 f) {f10(f);}\n"
"void f13(inout vec4 f) {f14(f);}\n"
"\n"},
{SR_FS,33,
"void f03(inout vec4 f);\n"
"void f07(inout vec4 f);\n"
"void f11(inout vec4 f);\n"
"void f15(inout vec4 f);\n"
"void f02(inout vec4 f) {f03(f);}\n"
"void f06(inout vec4 f) {f07(f);}\n"
"void f10(inout vec4 f) {f11(f);}\n"
"void f14(inout vec4 f) {f15(f);}\n"
"\n"},
{SR_FS,43,
"void f04(inout vec4 f);\n"
"void f08(inout vec4 f);\n"
"void f12(inout vec4 f);\n"
"void f16(inout float f);\n"
"void f03(inout vec4 f) {f04(f);}\n"
"void f07(inout vec4 f) {f08(f);}\n"
"void f11(inout vec4 f) {f12(f);}\n"
"void f15(inout vec4 f) {f16(f.y);}\n"
"\n"},
{SR_FS,53,
"void f05(inout vec4 f);\n"
"void f09(inout vec4 f);\n"
"void f13(inout vec4 f);\n"
"void f04(inout vec4 f) {f05(f);}\n"
"void f08(inout vec4 f) {f09(f);}\n"
"void f12(inout vec4 f) {f13(f);}\n"
"void f16(inout float f) {f=0.2;}\n"
"\n"
"\n"},
{SR_VS,10,
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,16,
"void f01(inout float f);\n"
"void f02(inout float f);\n"
"void f06(inout float f);\n"
"void f10(inout float f);\n"
"void f14(inout float f);\n"
"void main()\n"
"{\n"
"\tfloat g = 0.3;\n"
"\tf01(g);\n"
"\tgl_FragColor = vec4(0.0, g, 0, 1.0);\n"
"}\n"
"void f01(inout float f) {f02(f);}\n"
"void f05(inout float f) {f06(f);}\n"
"void f09(inout float f) {f10(f);}\n"
"void f13(inout float f) {f14(f);}\n"
"\n"},
{SR_FS,33,
"void f03(inout float f);\n"
"void f07(inout float f);\n"
"void f11(inout float f);\n"
"void f15(inout float f);\n"
"void f02(inout float f) {f03(f);}\n"
"void f06(inout float f) {f07(f);}\n"
"void f10(inout float f) {f11(f);}\n"
"void f14(inout float f) {f15(f);}\n"
"\n"},
{SR_FS,43,
"void f04(inout float f);\n"
"void f08(inout float f);\n"
"void f12(inout float f);\n"
"void f16(inout float f);\n"
"void f03(inout float f) {f04(f);}\n"
"void f07(inout float f) {f08(f);}\n"
"void f11(inout float f) {f12(f);}\n"
"void f15(inout float f) {f16(f);}\n"
"\n"},
{SR_FS,53,
"void f05(inout float f);\n"
"void f09(inout float f);\n"
"void f13(inout float f);\n"
"void f04(inout float f) {f05(f);}\n"
"void f08(inout float f) {f09(f);}\n"
"void f12(inout float f) {f13(f);}\n"
"void f16(inout float f) {f=0.2;}\n"
"\n"
"\n"},
{SR_FS,5,
"vec4 c1 = vec4(0.4, 0.5, 0.25, 0.0);\n"
"vec4 c2 = vec4(0.3, 0.5, 0.5,  0.4);\n"
"vec4 c3 = c1 + c2;\n"
"void main()\n"
"{\n"
"\tgl_FragColor = c3;\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"vec4 c = vec4(1.0, 0.5, 0.25, 0.0);\n"
"void main()\n"
"{\n"
"\tgl_FragColor = c;\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"void main()\n"
"{\n"
"\tmat4 m = mat4(\n"
"\t\t1.0, 0.5, 0.6, 0.0,  // col 0\n"
"\t\t0.0, 1.0, 0.0, 0.7,  // col 1\n"
"\t\t0.0, 0.0, 1.0, 0.8,  // col 2\n"
"\t\t0.1, 0.2, 0.3, 1.0   // col 3\n"
"\t);\n"
"\tvec4 color = gl_Color * m;\n"
"\tgl_FragColor = color;\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"void main()\n"
"{\n"
"\tmat4 m = mat4(\n"
"\t\t1.0, 0.5, 0.6, 0.0,  // col 0\n"
"\t\t0.0, 1.0, 0.0, 0.7,  // col 1\n"
"\t\t0.0, 0.0, 1.0, 0.8,  // col 2\n"
"\t\t0.1, 0.2, 0.3, 1.0   // col 3\n"
"\t);\n"
"\tvec4 color = m * gl_Color;\n"
"\tgl_FragColor = color;\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"void main()\n"
"{\n"
"\tmat4 m = mat4(0.5); // scale by 0.5\n"
"\tvec4 color = gl_Color * m;\n"
"\tgl_FragColor = color;\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"float Half(const in float x)\n"
"{\n"
"\treturn 0.5 * x;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"\tfloat a = 0.5;\n"
"\tfloat b = Half(Half(a));\n"
"\tgl_FragColor = vec4(b);\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"float Half(const in float x)\n"
"{\n"
"\treturn 0.5 * x;\n"
"}\n"
"\n"
"float square(const in float x)\n"
"{\n"
"\treturn x * x;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"\tfloat a = 0.5;\n"
"\tfloat b = square(Half(1.0));\n"
"\tgl_FragColor = vec4(b);\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"void main()\n"
"{\n"
"\tfloat x = 0.75;\n"
"\t// this should always be true\n"
"\tif (x >= 0.5 || ++x >= 0.0) {\n"
"\t\tx += 0.1;\n"
"\t}\n"
"\tgl_FragColor = vec4(x);\n"
"}\n"
"\n"
"\n"},
{SR_VS,10,
"#version 110\n"
"\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = (gl_Vertex + 1.0) / 2.0;\n"
"}\n"
"\n"},
{SR_FS,20,
"#version 110\n"
"\n"
"uniform sampler1DShadow tex;\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_FragColor = shadow1D(tex, vec3(texcoords.x, 0.0, texcoords.y));\n"
"}\n"
"\n"},
{SR_VS,10,
"#version 110\n"
"\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = (gl_Vertex + 1.0) / 2.0;\n"
"}\n"
"\n"},
{SR_FS,20,
"#version 110\n"
"\n"
"uniform sampler1DShadow tex;\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_FragColor = shadow1D(tex, vec3(texcoords.x, 0.0, texcoords.y));\n"
"}\n"
"\n"},
{SR_VS,10,
"#version 110\n"
"\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = (gl_Vertex + 1.0) / 2.0;\n"
"}\n"
"\n"},
{SR_FS,20,
"#version 110\n"
"\n"
"uniform sampler1DShadow tex;\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_FragColor = shadow1D(tex, vec3(texcoords.x, 0.0, texcoords.y));\n"
"}\n"
"\n"},
{SR_VS,10,
"#version 110\n"
"\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = (gl_Vertex + 1.0) / 2.0;\n"
"}\n"
"\n"},
{SR_FS,20,
"#version 110\n"
"\n"
"uniform sampler1DShadow tex;\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_FragColor = shadow1D(tex, vec3(texcoords.x, 0.0, texcoords.y));\n"
"}\n"
"\n"},
{SR_VS,10,
"#version 110\n"
"\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = (gl_Vertex + 1.0) / 2.0;\n"
"}\n"
"\n"},
{SR_FS,20,
"#version 110\n"
"\n"
"uniform sampler1DShadow tex;\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_FragColor = shadow1D(tex, vec3(texcoords.x, 0.0, texcoords.y));\n"
"}\n"
"\n"},
{SR_VS,10,
"#version 110\n"
"\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = (gl_Vertex + 1.0) / 2.0;\n"
"}\n"
"\n"},
{SR_FS,20,
"#version 110\n"
"\n"
"uniform sampler1DShadow tex;\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_FragColor = shadow1D(tex, vec3(texcoords.x, 0.0, texcoords.y));\n"
"}\n"
"\n"},
{SR_VS,10,
"#version 110\n"
"\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = (gl_Vertex + 1.0) / 2.0;\n"
"}\n"
"\n"},
{SR_FS,20,
"#version 110\n"
"\n"
"uniform sampler1DShadow tex;\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_FragColor = shadow1D(tex, vec3(texcoords.x, 0.0, texcoords.y));\n"
"}\n"
"\n"},
{SR_VS,10,
"#version 110\n"
"\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = (gl_Vertex + 1.0) / 2.0;\n"
"}\n"
"\n"},
{SR_FS,20,
"#version 110\n"
"\n"
"uniform sampler1DShadow tex;\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_FragColor = shadow1D(tex, vec3(texcoords.x, 0.0, texcoords.y));\n"
"}\n"
"\n"},
{SR_VS,10,
"#version 110\n"
"\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = (gl_Vertex + 1.0) / 2.0;\n"
"}\n"
"\n"},
{SR_FS,20,
"#version 110\n"
"\n"
"uniform sampler1DShadow tex;\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_FragColor = shadow1D(tex, vec3(texcoords.x, 0.0, texcoords.y));\n"
"}\n"
"\n"},
{SR_VS,5,
"#version 110\n"
"\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = (gl_Vertex + 1.0) / 2.0;\n"
"}\n"
"\n"},
{SR_FS,15,
"varying vec4 texcoords;\n"
"uniform sampler1DShadow tex;\n"
"\n"
"void main()\n"
"{\n"
"\tgl_FragColor = shadow1D(tex, texcoords.xyy, 1.0);\n"
"}\n"
"\n"},
{SR_VS,5,
"#version 110\n"
"\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = (gl_Vertex + 1.0) / 2.0;\n"
"}\n"
"\n"},
{SR_FS,15,
"varying vec4 texcoords;\n"
"uniform sampler1DShadow tex;\n"
"\n"
"void main()\n"
"{\n"
"\tgl_FragColor = shadow1D(tex, vec3(texcoords.x, 0.0, texcoords.y));\n"
"}\n"
"\n"},
{SR_VS,5,
"varying vec4 texcoords;\n"
"\n"
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = gl_Vertex + 1.0;\n"
"}\n"
"\n"},
{SR_FS,14,
"varying vec4 texcoords;\n"
"uniform sampler1DShadow tex;\n"
"\n"
"void main()\n"
"{\n"
"\tgl_FragColor = shadow1DProj(tex, vec4(texcoords.x, 0.0, texcoords.y, 2.0), 1.0);\n"
"}\n"
"\n"},
{SR_VS,5,
"varying vec4 texcoords;\n"
"\n"
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = gl_Vertex + 1.0;\n"
"}\n"
"\n"},
{SR_FS,14,
"varying vec4 texcoords;\n"
"uniform sampler1DShadow tex;\n"
"\n"
"void main()\n"
"{\n"
"\tgl_FragColor = shadow1DProj(tex, vec4(texcoords.x, 0.0, texcoords.y, 2.0));\n"
"}\n"
"\n"},
{SR_VS,10,
"#version 110\n"
"\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = (gl_Vertex + 1.0) / 2.0;\n"
"}\n"
"\n"},
{SR_FS,20,
"#version 110\n"
"\n"
"uniform sampler2DShadow tex;\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_FragColor = shadow2D(tex, texcoords.xyy);\n"
"}\n"
"\n"},
{SR_VS,10,
"#version 110\n"
"\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = (gl_Vertex + 1.0) / 2.0;\n"
"}\n"
"\n"},
{SR_FS,20,
"#version 110\n"
"\n"
"uniform sampler2DShadow tex;\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_FragColor = shadow2D(tex, texcoords.xyy);\n"
"}\n"
"\n"},
{SR_VS,10,
"#version 110\n"
"\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = (gl_Vertex + 1.0) / 2.0;\n"
"}\n"
"\n"},
{SR_FS,20,
"#version 110\n"
"\n"
"uniform sampler2DShadow tex;\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_FragColor = shadow2D(tex, texcoords.xyy);\n"
"}\n"
"\n"},
{SR_VS,10,
"#version 110\n"
"\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = (gl_Vertex + 1.0) / 2.0;\n"
"}\n"
"\n"},
{SR_FS,20,
"#version 110\n"
"\n"
"uniform sampler2DShadow tex;\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_FragColor = shadow2D(tex, texcoords.xyy);\n"
"}\n"
"\n"},
{SR_VS,10,
"#version 110\n"
"\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = (gl_Vertex + 1.0) / 2.0;\n"
"}\n"
"\n"},
{SR_FS,20,
"#version 110\n"
"\n"
"uniform sampler2DShadow tex;\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_FragColor = shadow2D(tex, texcoords.xyy);\n"
"}\n"
"\n"},
{SR_VS,10,
"#version 110\n"
"\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = (gl_Vertex + 1.0) / 2.0;\n"
"}\n"
"\n"},
{SR_FS,20,
"#version 110\n"
"\n"
"uniform sampler2DShadow tex;\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_FragColor = shadow2D(tex, texcoords.xyy);\n"
"}\n"
"\n"},
{SR_VS,10,
"#version 110\n"
"\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = (gl_Vertex + 1.0) / 2.0;\n"
"}\n"
"\n"},
{SR_FS,20,
"#version 110\n"
"\n"
"uniform sampler2DShadow tex;\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_FragColor = shadow2D(tex, texcoords.xyy);\n"
"}\n"
"\n"},
{SR_VS,10,
"#version 110\n"
"\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = (gl_Vertex + 1.0) / 2.0;\n"
"}\n"
"\n"},
{SR_FS,20,
"#version 110\n"
"\n"
"uniform sampler2DShadow tex;\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_FragColor = shadow2D(tex, texcoords.xyy);\n"
"}\n"
"\n"},
{SR_VS,10,
"#version 110\n"
"\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = (gl_Vertex + 1.0) / 2.0;\n"
"}\n"
"\n"},
{SR_FS,20,
"#version 110\n"
"\n"
"uniform sampler2DShadow tex;\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_FragColor = shadow2D(tex, texcoords.xyy);\n"
"}\n"
"\n"},
{SR_VS,5,
"#version 110\n"
"\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = (gl_Vertex + 1.0) / 2.0;\n"
"}\n"
"\n"},
{SR_FS,15,
"varying vec4 texcoords;\n"
"uniform sampler2DShadow tex;\n"
"\n"
"void main()\n"
"{\n"
"\tgl_FragColor = shadow2D(tex, texcoords.xyy, 1.0);\n"
"}\n"
"\n"},
{SR_VS,14,
"#version 110\n"
"\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = (gl_Vertex + 1.0) / 2.0;\n"
"}\n"
"\n"},
{SR_FS,24,
"uniform vec3 texcoords;\n"
"uniform sampler2DShadow tex;\n"
"\n"
"void main()\n"
"{\n"
"\tgl_FragColor = shadow2D(tex, texcoords.xyz);\n"
"}\n"
"\n"},
{SR_VS,5,
"#version 110\n"
"\n"
"varying vec4 texcoords;\n"
"\n"
"void main() {\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = (gl_Vertex + 1.0) / 2.0;\n"
"}\n"
"\n"},
{SR_FS,15,
"varying vec4 texcoords;\n"
"uniform sampler2DShadow tex;\n"
"\n"
"void main()\n"
"{\n"
"\tgl_FragColor = shadow2D(tex, texcoords.xyy);\n"
"}\n"
"\n"},
{SR_VS,5,
"varying vec4 texcoords;\n"
"\n"
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = gl_Vertex + 1.0;\n"
"}\n"
"\n"},
{SR_FS,14,
"varying vec4 texcoords;\n"
"uniform sampler2DShadow tex;\n"
"\n"
"void main()\n"
"{\n"
"\tgl_FragColor = shadow2DProj(tex, vec4(texcoords.xyy, 2.0), 1.0);\n"
"}\n"
"\n"},
{SR_VS,5,
"varying vec4 texcoords;\n"
"\n"
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = gl_Vertex + 1.0;\n"
"}\n"
"\n"},
{SR_FS,14,
"varying vec4 texcoords;\n"
"uniform sampler2DShadow tex;\n"
"\n"
"void main()\n"
"{\n"
"\tgl_FragColor = shadow2DProj(tex, vec4(texcoords.xyy, 2.0));\n"
"}\n"
"\n"},
{SR_VS,5,
"varying vec4 texcoords;\n"
"\n"
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = (gl_Vertex + 1.0) / 2.0;\n"
"}\n"
"\n"},
{SR_FS,14,
"varying vec4 texcoords;\n"
"uniform sampler2D tex[2];\n"
"\n"
"vec4 foo(in sampler2D foo_tex[2], vec4 foo_texcoords)\n"
"{\n"
"\tif (foo_texcoords.x < 0.5)\n"
"\t\treturn texture2D(foo_tex[0], foo_texcoords.xy);\n"
"\telse\n"
"\t\treturn texture2D(foo_tex[1], foo_texcoords.xy);\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"\tgl_FragColor = foo(tex, texcoords);\n"
"}\n"
"\n"},
{SR_VS,5,
"varying vec4 texcoords;\n"
"\n"
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = (gl_Vertex + 1.0) / 2.0;\n"
"}\n"
"\n"},
{SR_FS,14,
"varying vec4 texcoords;\n"
"struct S1 {\n"
"\tfloat f;\n"
"\tsampler2D tex;\n"
"};\n"
"struct S2 {\n"
"\tfloat f;\n"
"\tS1 s;\n"
"};\n"
"uniform S2 s;\n"
"\n"
"vec4 foo(in S2 foo_s, vec4 foo_texcoords)\n"
"{\n"
"\treturn texture2D(foo_s.s.tex, foo_texcoords.xy) * foo_s.s.f * foo_s.f;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"\tgl_FragColor = foo(s, texcoords);\n"
"}\n"
"\n"},
{SR_VS,5,
"varying vec4 texcoords;\n"
"\n"
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = (gl_Vertex + 1.0) / 2.0;\n"
"}\n"
"\n"},
{SR_FS,14,
"varying vec4 texcoords;\n"
"struct S {\n"
"\tfloat f;\n"
"\tsampler2D tex[1];\n"
"};\n"
"uniform S s;\n"
"\n"
"vec4 foo(in S foo_s, vec4 foo_texcoords)\n"
"{\n"
"\treturn texture2D(foo_s.tex[0], foo_texcoords.xy) * foo_s.f;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"\tgl_FragColor = foo(s, texcoords);\n"
"}\n"
"\n"},
{SR_VS,5,
"varying vec4 texcoords;\n"
"\n"
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = (gl_Vertex + 1.0) / 2.0;\n"
"}\n"
"\n"},
{SR_FS,14,
"varying vec4 texcoords;\n"
"struct S {\n"
"\tfloat f;\n"
"\tsampler2D tex;\n"
"};\n"
"uniform S s;\n"
"\n"
"vec4 foo(in S foo_s, vec4 foo_texcoords)\n"
"{\n"
"\treturn texture2D(foo_s.tex, foo_texcoords.xy) * foo_s.f;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"\tgl_FragColor = foo(s, texcoords);\n"
"}\n"
"\n"},
{SR_VS,5,
"varying vec4 texcoords;\n"
"\n"
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = (gl_Vertex + 1.0) / 2.0;\n"
"}\n"
"\n"},
{SR_FS,14,
"varying vec4 texcoords;\n"
"uniform float f;\n"
"uniform sampler2D tex;\n"
"\n"
"vec4 foo(float foo_f, in sampler2D foo_tex, vec4 foo_texcoords)\n"
"{\n"
"\treturn texture2D(foo_tex, foo_texcoords.xy) * foo_f;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"\tgl_FragColor = foo(f, tex, texcoords);\n"
"}\n"
"\n"},
{SR_VS,5,
"varying vec4 texcoords;\n"
"\n"
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = (gl_Vertex + 1.0) / 2.0;\n"
"}\n"
"\n"},
{SR_FS,14,
"varying vec4 texcoords;\n"
"struct S {\n"
"\tfloat f;\n"
"\tsampler2D tex;\n"
"};\n"
"uniform S s;\n"
"\n"
"vec4 foo(S foo_s, vec4 foo_texcoords)\n"
"{\n"
"\treturn texture2D(foo_s.tex, foo_texcoords.xy) * foo_s.f;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"\tgl_FragColor = foo(s, texcoords);\n"
"}\n"
"\n"},
{SR_VS,5,
"varying vec4 texcoords;\n"
"\n"
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"\ttexcoords = (gl_Vertex + 1.0) / 2.0;\n"
"}\n"
"\n"},
{SR_FS,14,
"varying vec4 texcoords;\n"
"uniform float f;\n"
"uniform sampler2D tex;\n"
"\n"
"vec4 foo(float foo_f, sampler2D foo_tex, vec4 foo_texcoords)\n"
"{\n"
"\treturn texture2D(foo_tex, foo_texcoords.xy) * foo_f;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"\tgl_FragColor = foo(f, tex, texcoords);\n"
"}\n"
"\n"},
{SR_FS,5,
"void main()\n"
"{\n"
"\tfloat x, y, z;\n"
"\tx = 1.0, y = 0.5, z = x * y;\n"
"\tgl_FragColor = vec4(z);\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"struct s1 {\n"
"  float f1;\n"
"  vec4 v4;\n"
"};\n"
"\n"
"void main()\n"
"{\n"
"\ts1 a[2];\n"
"\ta[0].v4 = vec4(0.25, 0.5, 0.75, 1.0);\n"
"\ta[0].f1 = 0.0;\n"
"\ta[1] = a[0];\n"
"\tgl_FragColor = a[1].v4;\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"struct s1 {\n"
"  float f1;\n"
"  vec4 v4;\n"
"};\n"
"\n"
"void main()\n"
"{\n"
"\n"
"\ts1 a, b;\n"
"\ta.v4 = vec4(0.25, 0.5, 0.75, 1.0);\n"
"\ta.f1 = 0.0;\n"
"\tb = a;\n"
"\tgl_FragColor = b.v4;\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"struct s1 {\n"
"\tfloat foo;\n"
"\tvec4 v4;\n"
"};\n"
"struct s2 {\n"
"\tfloat bar;\n"
"\ts1 s;\n"
"\tfloat baz;\n"
"};\n"
"\n"
"void main()\n"
"{\n"
"\ts2 a;\n"
"\ta.s.v4 = vec4(0.25, 0.5, 0.75, 1.0);\n"
"\ta.bar = 0.0;\n"
"\ta.baz = 0.0;\n"
"\ta.s.foo = 0.0;\n"
"\tgl_FragColor = a.s.v4;\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"struct s1 {\n"
"  float f1;\n"
"  vec4 v4;\n"
"};\n"
"\n"
"void main()\n"
"{\n"
"\tvec4 scale = vec4(0.5);\n"
"\tvec4 bias = vec4(0.1);\n"
"\ts1 a;\n"
"\ta.v4 = vec4(0.25, 0.5, 0.75, 1.0);\n"
"\ta.f1 = 0.0;\n"
"\tgl_FragColor = a.v4 * scale + bias;\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"void main()\n"
"{\n"
"\tvec4 a = vec4(0.5,  0.25, 0.0, 1.0);\n"
"\tgl_FragColor = a.grrb;\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"void main()\n"
"{\n"
"\tvec4 a = vec4(0.5,  0.25, 0.0, 1.0);\n"
"\tgl_FragColor = a.tssp;\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"void main()\n"
"{\n"
"\tvec4 a = vec4(0.5,  0.25, 0.0, 1.0);\n"
"\tgl_FragColor = a.yxxz;\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"void main()\n"
"{\n"
"\tvec4 a = vec4(1, 1, 1, 1);\n"
"\tvec4 b = vec4(0.5, 0.2, 0.1, 0.8);\n"
"\tvec4 c = (a * b).wzyx;\n"
"\tgl_FragColor = c;\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"void main()\n"
"{\n"
"\tvec4 a = vec4(0.1, 0.2, 0.3, 0.4);\n"
"\tvec4 b = a.wzyx.yxwz.xxyz;\n"
"\tgl_FragColor = b;\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"void main()\n"
"{\n"
"\tgl_FragColor.bg = vec2(1.0, 0.5);\n"
"\tgl_FragColor.ar = vec2(0.25, 0.75);\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"void main()\n"
"{\n"
"\tgl_FragColor.zy = vec2(1.0, 0.5);\n"
"\tgl_FragColor.wx = vec2(0.25, 0.75);\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"void main()\n"
"{\n"
"\tgl_FragColor.pt = vec2(1.0, 0.5);\n"
"\tgl_FragColor.qs = vec2(0.25, 0.75);\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"void main()\n"
"{\n"
"\tgl_FragColor.zwxy = vec4(1.0, 0.5, 0.25, 0.75);\n"
"}\n"
"\n"
"\n"},
{SR_VS,5,
"\n"
"/**\n"
" * This test exercises arrays indexed with a variable.\n"
" * We test with two arrays to be sure at least one is positioned\n"
" * above TEMP[0].\n"
" */\n"
"\n"
"uniform float one;\n"
"uniform int writeIndex;\n"
"uniform int readIndex;\n"
"varying vec4 color;\n"
"void main()\n"
"{\n"
"\t// Setup two arrays\n"
"\tvec4 a1[5], a2[5];\n"
"\n"
"\ta1[0] = vec4(0.0, 0.0, 0.0, 0.0);\n"
"\ta1[1] = vec4(0.0, 0.0, 0.0, 0.0);\n"
"\ta1[2] = vec4(0.0, 0.0, 0.0, 0.0);\n"
"\ta1[3] = vec4(0.0, 0.0, 0.0, 0.0);\n"
"\ta1[4] = vec4(0.0, 0.0, 0.0, 0.0);\n"
"\n"
"\ta2[0] = vec4(0.0, 0.0, 0.0, 0.0);\n"
"\ta2[1] = vec4(0.0, 0.0, 0.0, 0.0);\n"
"\ta2[2] = vec4(0.0, 0.0, 0.0, 0.0);\n"
"\ta2[3] = vec4(0.0, 0.0, 0.0, 0.0);\n"
"\ta2[4] = vec4(0.0, 0.0, 0.25, 0.0);\n"
"\n"
"\tgl_Position = gl_Vertex;\n"
"\n"
"\t// write to the arrays\n"
"\ta1[writeIndex] = vec4(1.0, 0.25, 0.0, 1.0);\n"
"\ta2[writeIndex] = vec4(1.0, 0.25, 0.75, 1.0);\n"
"\n"
"\t// add array elements\n"
"\tcolor = a1[readIndex] + a2[readIndex] + a2[4];\n"
"}\n"
"\n"},
{SR_FS,44,
"varying vec4 color;\n"
"void main()\n"
"{\n"
"\tgl_FragColor = color;\n"
"}\n"
"\n"},
{SR_FS,5,
"uniform sampler1D tex1d;\n"
"void main()\n"
"{\n"
"\tgl_FragColor = texture1D(tex1d, gl_TexCoord[0].x);\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"uniform sampler2D tex2d;\n"
"void main()\n"
"{\n"
"\tvec2 coord = gl_TexCoord[0].xy + vec2(0.5);\n"
"\tgl_FragColor = texture2D(tex2d, coord, 0.0);\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"uniform sampler3D tex3d;\n"
"void main()\n"
"{\n"
"\tvec3 coord = gl_TexCoord[0].xyz;\n"
"\tcoord.y = 0.25;\n"
"\tcoord.z = 0.75;\n"
"\tgl_FragColor = texture3D(tex3d, coord);\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"uniform sampler3D tex3d;\n"
"void main()\n"
"{\n"
"\tgl_FragColor = texture3D(tex3d, gl_TexCoord[0].xyz);\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"uniform mat4 uniformMat4;\n"
"void main()\n"
"{\n"
"\tgl_FragColor = uniformMat4[3];\n"
"}\n"
"\n"
"\n"},
{SR_VS,8,
"#version 110\n"
"\n"
"void main()\n"
"{\n"
"\tgl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,16,
"#version 110\n"
"\n"
"uniform float zero0, zero1, zero2;\n"
"uniform float u;\n"
"\n"
"void main()\n"
"{\n"
"    gl_FragColor = u + zero0 * gl_FragCoord + zero1 + zero2;\n"
"}\n"
"\n"},
{SR_VS,5,
"varying float m1[4];\n"
"varying float m2[4];\n"
"\n"
"void main()\n"
"{\n"
"    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
"\n"
"    m1[0] = 1.0;\n"
"    m1[1] = 2.0;\n"
"    m1[2] = 3.0;\n"
"    m1[3] = 4.0;\n"
"    m2[0] = 5.0;\n"
"    m2[1] = 6.0;\n"
"    m2[2] = 7.0;\n"
"    m2[3] = 8.0;\n"
"}\n"
"\n"},
{SR_FS,23,
"uniform int index;\n"
"uniform float expect;\n"
"varying float m1[4];\n"
"varying float m2[4];\n"
"\n"
"void main()\n"
"{\n"
"    gl_FragColor = ((index >= 4 ? m2[index-4] : m1[index]) == expect)\n"
"        ? vec4(0.0, 1.0, 0.0, 1.0) : vec4(1.0, 0.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_VS,5,
"varying vec2 m1[4];\n"
"varying vec2 m2[4];\n"
"\n"
"void main()\n"
"{\n"
"    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
"\n"
"    m1[0] = vec2(1.0, 1.1);\n"
"    m1[1] = vec2(2.0, 2.1);\n"
"    m1[2] = vec2(3.0, 3.1);\n"
"    m1[3] = vec2(4.0, 4.1);\n"
"    m2[0] = vec2(5.0, 5.1);\n"
"    m2[1] = vec2(6.0, 6.1);\n"
"    m2[2] = vec2(7.0, 7.1);\n"
"    m2[3] = vec2(8.0, 8.1);\n"
"}\n"
"\n"},
{SR_FS,23,
"uniform int index;\n"
"uniform vec2 expect;\n"
"varying vec2 m1[4];\n"
"varying vec2 m2[4];\n"
"\n"
"void main()\n"
"{\n"
"    gl_FragColor = ((index >= 4 ? m2[index-4] : m1[index]) == expect)\n"
"        ? vec4(0.0, 1.0, 0.0, 1.0) : vec4(1.0, 0.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_VS,5,
"varying vec3 m1[4];\n"
"varying vec3 m2[4];\n"
"\n"
"void main()\n"
"{\n"
"    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
"\n"
"    m1[0] = vec3(1.0, 1.1, 1.2);\n"
"    m1[1] = vec3(2.0, 2.1, 2.2);\n"
"    m1[2] = vec3(3.0, 3.1, 3.2);\n"
"    m1[3] = vec3(4.0, 4.1, 4.2);\n"
"    m2[0] = vec3(5.0, 5.1, 5.2);\n"
"    m2[1] = vec3(6.0, 6.1, 6.2);\n"
"    m2[2] = vec3(7.0, 7.1, 7.2);\n"
"    m2[3] = vec3(8.0, 8.1, 8.2);\n"
"}\n"
"\n"},
{SR_FS,23,
"uniform int index;\n"
"uniform vec3 expect;\n"
"varying vec3 m1[4];\n"
"varying vec3 m2[4];\n"
"\n"
"void main()\n"
"{\n"
"    gl_FragColor = ((index >= 4 ? m2[index-4] : m1[index]) == expect)\n"
"        ? vec4(0.0, 1.0, 0.0, 1.0) : vec4(1.0, 0.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_VS,5,
"varying vec4 m1[4];\n"
"varying vec4 m2[4];\n"
"\n"
"void main()\n"
"{\n"
"    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
"\n"
"    m1[0] = vec4(1.0, 1.1, 1.2, 1.3);\n"
"    m1[1] = vec4(2.0, 2.1, 2.2, 2.3);\n"
"    m1[2] = vec4(3.0, 3.1, 3.2, 3.3);\n"
"    m1[3] = vec4(4.0, 4.1, 4.2, 4.3);\n"
"    m2[0] = vec4(5.0, 5.1, 5.2, 5.3);\n"
"    m2[1] = vec4(6.0, 6.1, 6.2, 6.3);\n"
"    m2[2] = vec4(7.0, 7.1, 7.2, 7.3);\n"
"    m2[3] = vec4(8.0, 8.1, 8.2, 8.3);\n"
"}\n"
"\n"},
{SR_FS,23,
"uniform int index;\n"
"uniform vec4 expect;\n"
"varying vec4 m1[4];\n"
"varying vec4 m2[4];\n"
"\n"
"void main()\n"
"{\n"
"    gl_FragColor = ((index >= 4 ? m2[index-4] : m1[index]) == expect)\n"
"        ? vec4(0.0, 1.0, 0.0, 1.0) : vec4(1.0, 0.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_VS,5,
"uniform int index;\n"
"varying float m1[4];\n"
"varying float m2[4];\n"
"\n"
"void main()\n"
"{\n"
"    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
"\n"
"    m1[0] = 1.0;\n"
"    m1[1] = 2.0;\n"
"    m1[2] = 3.0;\n"
"    m1[3] = 4.0;\n"
"    m2[0] = 5.0;\n"
"    m2[1] = 6.0;\n"
"    m2[2] = 7.0;\n"
"    m2[3] = 8.0;\n"
"\n"
"    if (index >= 4)\n"
"        m2[index-4] = 0.0;\n"
"    else\n"
"        m1[index] = 0.0;\n"
"}\n"
"\n"},
{SR_FS,29,
"uniform int index;\n"
"varying float m1[4];\n"
"varying float m2[4];\n"
"\n"
"void main()\n"
"{\n"
"    bool pass = true;\n"
"\n"
"    for (int i = 0; i < 4; i++)\n"
"        pass = pass && m1[i] == (index == i ? 0.0 : float(1+i));\n"
"\n"
"    for (int i = 0; i < 4; i++)\n"
"        pass = pass && m2[i] == (index == 4+i ? 0.0 : float(5+i));\n"
"\n"
"    gl_FragColor = pass ? vec4(0.0, 1.0, 0.0, 1.0) : vec4(1.0, 0.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_VS,5,
"uniform int index;\n"
"varying vec2 m1[4];\n"
"varying vec2 m2[4];\n"
"\n"
"void main()\n"
"{\n"
"    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
"\n"
"    m1[0] = vec2(1.0, 1.1);\n"
"    m1[1] = vec2(2.0, 2.1);\n"
"    m1[2] = vec2(3.0, 3.1);\n"
"    m1[3] = vec2(4.0, 4.1);\n"
"    m2[0] = vec2(5.0, 5.1);\n"
"    m2[1] = vec2(6.0, 6.1);\n"
"    m2[2] = vec2(7.0, 7.1);\n"
"    m2[3] = vec2(8.0, 8.1);\n"
"\n"
"    if (index >= 4)\n"
"        m2[index-4] = vec2(0.0, 0.1);\n"
"    else\n"
"        m1[index] = vec2(0.0, 0.1);\n"
"}\n"
"\n"},
{SR_FS,29,
"uniform int index;\n"
"varying vec2 m1[4];\n"
"varying vec2 m2[4];\n"
"\n"
"void main()\n"
"{\n"
"    bool pass = true;\n"
"\n"
"    for (int i = 0; i < 4; i++)\n"
"        pass = pass && m1[i] == (index == i ? vec2(0.0, 0.1) : vec2(1.0, 1.1) + vec2(i));\n"
"\n"
"    for (int i = 0; i < 4; i++)\n"
"        pass = pass && m2[i] == (index == 4+i ? vec2(0.0, 0.1) : vec2(5.0, 5.1) + vec2(i));\n"
"\n"
"    gl_FragColor = pass ? vec4(0.0, 1.0, 0.0, 1.0) : vec4(1.0, 0.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_VS,5,
"uniform int index;\n"
"varying vec3 m1[4];\n"
"varying vec3 m2[4];\n"
"\n"
"void main()\n"
"{\n"
"    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
"\n"
"    m1[0] = vec3(1.0, 1.1, 1.2);\n"
"    m1[1] = vec3(2.0, 2.1, 2.2);\n"
"    m1[2] = vec3(3.0, 3.1, 3.2);\n"
"    m1[3] = vec3(4.0, 4.1, 4.2);\n"
"    m2[0] = vec3(5.0, 5.1, 5.2);\n"
"    m2[1] = vec3(6.0, 6.1, 6.2);\n"
"    m2[2] = vec3(7.0, 7.1, 7.2);\n"
"    m2[3] = vec3(8.0, 8.1, 8.2);\n"
"\n"
"    if (index >= 4)\n"
"        m2[index-4] = vec3(0.0, 0.1, 0.2);\n"
"    else\n"
"        m1[index] = vec3(0.0, 0.1, 0.2);\n"
"}\n"
"\n"},
{SR_FS,29,
"uniform int index;\n"
"varying vec3 m1[4];\n"
"varying vec3 m2[4];\n"
"\n"
"void main()\n"
"{\n"
"    bool pass = true;\n"
"\n"
"    for (int i = 0; i < 4; i++)\n"
"        pass = pass && m1[i] == (index == i ? vec3(0.0, 0.1, 0.2) : vec3(1.0, 1.1, 1.2) + vec3(i));\n"
"\n"
"    for (int i = 0; i < 4; i++)\n"
"        pass = pass && m2[i] == (index == 4+i ? vec3(0.0, 0.1, 0.2) : vec3(5.0, 5.1, 5.2) + vec3(i));\n"
"\n"
"    gl_FragColor = pass ? vec4(0.0, 1.0, 0.0, 1.0) : vec4(1.0, 0.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_VS,5,
"uniform int index;\n"
"varying vec4 m1[4];\n"
"varying vec4 m2[4];\n"
"\n"
"void main()\n"
"{\n"
"    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
"\n"
"    m1[0] = vec4(1.0, 1.1, 1.2, 1.3);\n"
"    m1[1] = vec4(2.0, 2.1, 2.2, 2.3);\n"
"    m1[2] = vec4(3.0, 3.1, 3.2, 3.3);\n"
"    m1[3] = vec4(4.0, 4.1, 4.2, 4.3);\n"
"    m2[0] = vec4(5.0, 5.1, 5.2, 5.3);\n"
"    m2[1] = vec4(6.0, 6.1, 6.2, 6.3);\n"
"    m2[2] = vec4(7.0, 7.1, 7.2, 7.3);\n"
"    m2[3] = vec4(8.0, 8.1, 8.2, 8.3);\n"
"\n"
"    if (index >= 4)\n"
"        m2[index-4] = vec4(0.0, 0.1, 0.2, 0.3);\n"
"    else\n"
"        m1[index] = vec4(0.0, 0.1, 0.2, 0.3);\n"
"}\n"
"\n"},
{SR_FS,29,
"uniform int index;\n"
"varying vec4 m1[4];\n"
"varying vec4 m2[4];\n"
"\n"
"void main()\n"
"{\n"
"    bool pass = true;\n"
"\n"
"    for (int i = 0; i < 4; i++)\n"
"        pass = pass && m1[i] == (index == i ? vec4(0.0, 0.1, 0.2, 0.3) :\n"
"                                              vec4(1.0, 1.1, 1.2, 1.3) + vec4(i));\n"
"\n"
"    for (int i = 0; i < 4; i++)\n"
"        pass = pass && m2[i] == (index == 4+i ? vec4(0.0, 0.1, 0.2, 0.3) :\n"
"                                                vec4(5.0, 5.1, 5.2, 5.3) + vec4(i));\n"
"\n"
"    gl_FragColor = pass ? vec4(0.0, 1.0, 0.0, 1.0) : vec4(1.0, 0.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_VS,5,
"\n"
"varying vec4 c;\n"
"uniform bool b;\n"
"\n"
"void good(void)\n"
"{\n"
"    c = vec4(0, 1, 0, 1);\n"
"}\n"
"\n"
"void bad(void)\n"
"{\n"
"    c = vec4(1, 0, 0, 1);\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"    gl_Position = gl_Vertex;\n"
"    b ? good() : bad();\n"
"}\n"
"\n"},
{SR_FS,26,
"\n"
"varying vec4 c;\n"
"uniform bool b;\n"
"\n"
"void good(void)\n"
"{\n"
"    gl_FragColor = c;\n"
"}\n"
"\n"
"void bad(void)\n"
"{\n"
"    gl_FragColor = c + vec4(0, 0, 1, 0);\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"    b ? good() : bad();\n"
"}\n"
"\n"},
{SR_VS,9,
"int i;\n"
"int num_calls_to_foo;\n"
"attribute vec4 piglit_vertex;\n"
"varying vec4 color;\n"
"\n"
"void foo()\n"
"{\n"
"  num_calls_to_foo++;\n"
"  i = 2;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"  num_calls_to_foo = 0;\n"
"  for (i = 0; i < 3; i++) { // Executes once, since foo() sets i to 2\n"
"    for (int j = 0; j < 3; j++) { // Executes 3 times\n"
"      foo();\n"
"    }\n"
"  }\n"
"  gl_Position = piglit_vertex;\n"
"  if (num_calls_to_foo == 3)\n"
"    color = vec4(0.0, 1.0, 0.0, 1.0);\n"
"  else\n"
"    color = vec4(1.0, 0.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_FS,36,
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"  gl_FragColor = color;\n"
"}\n"
"\n"},
{SR_VS,5,
"uniform vec4 a;\n"
"varying vec4 color;\n"
"attribute vec4 piglit_vertex;\n"
"\n"
"void main()\n"
"{\n"
"    color = vec4(equal(piglit_vertex.w * a, vec4(0)));\n"
"    gl_Position = piglit_vertex;\n"
"}\n"
"\n"},
{SR_FS,16,
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"    gl_FragColor = color;\n"
"}\n"
"\n"},
{SR_VS,5,
"uniform int early_return;\n"
"varying float foo1;\n"
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"\tfoo1 = 0.5;\n"
"\tif (early_return != 0) {\n"
"\t\tfoo1 = 0.2;\n"
"\t\treturn;\n"
"\t}\n"
"}\n"
"\n"},
{SR_FS,18,
"varying float foo1;\n"
"void main()\n"
"{\n"
"\tgl_FragColor = vec4(foo1);\n"
"}\n"
"\n"},
{SR_VS,5,
"uniform int early_return;\n"
"varying float foo1;\n"
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"\tfoo1 = 0.5;\n"
"\tif (early_return != 0)\n"
"\t\treturn;\n"
"\tfoo1 = 0.2;\n"
"}\n"
"\n"},
{SR_FS,17,
"varying float foo1;\n"
"void main()\n"
"{\n"
"\tgl_FragColor = vec4(foo1);\n"
"}\n"
"\n"},
{SR_VS,9,
"attribute vec4 piglit_vertex;\n"
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"  int num_total_iterations = 0;\n"
"  int i = 0;\n"
"  while (i < 6) { // Executes twice, since inner loop increments i each time through\n"
"    for (int j = 0; j < 3; j++) { // Executes 3 times\n"
"      num_total_iterations++;\n"
"      i++;\n"
"    }\n"
"  }\n"
"  gl_Position = piglit_vertex;\n"
"  if (num_total_iterations == 6)\n"
"    color = vec4(0.0, 1.0, 0.0, 1.0);\n"
"  else\n"
"    color = vec4(1.0, 0.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_FS,30,
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"  gl_FragColor = color;\n"
"}\n"
"\n"},
{SR_VS,9,
"attribute vec4 piglit_vertex;\n"
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"  int num_total_iterations = 0;\n"
"  for (int i = 0; i < 3; i++) { // Executes once, since inner loop sets i to 2\n"
"    for (int j = 0; j < 3; j++) { // Executes 3 times\n"
"      num_total_iterations++;\n"
"      i = 2;\n"
"    }\n"
"  }\n"
"  gl_Position = piglit_vertex;\n"
"  if (num_total_iterations == 3)\n"
"    color = vec4(0.0, 1.0, 0.0, 1.0);\n"
"  else\n"
"    color = vec4(1.0, 0.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_FS,29,
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"  gl_FragColor = color;\n"
"}\n"
"\n"},
{SR_VS,5,
"uniform ivec4 v;\n"
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"\tint i = v.x + v.y + v.z + v.w;\n"
"\n"
"\tcolor = (i == 27) ? vec4(0, 1, 0, 1) : vec4(1, 0, 0, 1);\n"
"\tgl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,17,
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"\tgl_FragColor = color;\n"
"}\n"
"\n"},
{SR_VS,7,
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"  int sum;\n"
"  int i;\n"
"  bool failed = false;\n"
"\n"
"  sum = 0; for (i = 10; i < 14; i++) sum += i; if (sum != 46) failed = true;\n"
"  sum = 0; for (i = 10; i <= 13; i++) sum += i; if (sum != 46) failed = true;\n"
"  sum = 0; for (i = 10; 14 > i; i++) sum += i; if (sum != 46) failed = true;\n"
"  sum = 0; for (i = 10; 13 >= i; i++) sum += i; if (sum != 46) failed = true;\n"
"  sum = 0; for (i = 10; i != 14; i++) sum += i; if (sum != 46) failed = true;\n"
"  sum = 0; for (i = 10; 14 != i; i++) sum += i; if (sum != 46) failed = true;\n"
"  sum = 0; for (i = 10; i == 10; i++) sum += i; if (sum != 10) failed = true;\n"
"  sum = 0; for (i = 10; 10 == i; i++) sum += i; if (sum != 10) failed = true;\n"
"\n"
"  sum = 0; for (i = 13; i > 9; i--) sum += i; if (sum != 46) failed = true;\n"
"  sum = 0; for (i = 13; i >= 10; i--) sum += i; if (sum != 46) failed = true;\n"
"  sum = 0; for (i = 13; 9 < i; i--) sum += i; if (sum != 46) failed = true;\n"
"  sum = 0; for (i = 13; 10 <= i; i--) sum += i; if (sum != 46) failed = true;\n"
"  sum = 0; for (i = 13; i != 9; i--) sum += i; if (sum != 46) failed = true;\n"
"  sum = 0; for (i = 13; 9 != i; i--) sum += i; if (sum != 46) failed = true;\n"
"  sum = 0; for (i = 13; i == 13; i--) sum += i; if (sum != 13) failed = true;\n"
"  sum = 0; for (i = 13; 13 == i; i--) sum += i; if (sum != 13) failed = true;\n"
"\n"
"  gl_FrontColor = failed ? vec4(1.0, 0.0, 0.0, 1.0) : vec4(0.0, 1.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_FS,36,
"void main()\n"
"{\n"
"  gl_FragColor = gl_Color;\n"
"}\n"
"\n"},
{SR_VS,10,
"uniform int a;\n"
"\n"
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"\n"
"  vec4 colour = vec4(1.0, 0.0, 0.0, 1.0);\n"
"\n"
"  int i = 0;\n"
"  int j = 0;\n"
"  do {\n"
"    if (i == 2) {\n"
"      colour = vec4(0.0, 0.0, 1.0, 1.0);\n"
"      j++; // we use this so the if doesn't get reduced to a series of bcsel\n"
"    } else {\n"
"      colour = vec4(0.0, 1.0, 0.0, 1.0);\n"
"    }\n"
"\n"
"    i++;\n"
"    j++;\n"
"\n"
"    if (i >= 3) {\n"
"      if (j != 4)\n"
"         colour = vec4(1.0, 0.0, 0.0, 1.0);\n"
"      break;\n"
"    }\n"
"  } while (a == 1);\n"
"\n"
"  gl_FrontColor = colour;\n"
"}\n"
"\n"},
{SR_FS,42,
"void main()\n"
"{\n"
"  gl_FragColor = gl_Color;\n"
"}\n"
"\n"},
{SR_VS,10,
"uniform int a;\n"
"\n"
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"\n"
"  vec4 colour = vec4(1.0, 0.0, 0.0, 1.0);\n"
"\n"
"  int i = 0;\n"
"  int j = 0;\n"
"  do {\n"
"    i++;\n"
"    j++;\n"
"\n"
"    if (i >= 3) {\n"
"      if (i == 3) {\n"
"         colour = vec4(0.0, 0.0, 1.0, 1.0);\n"
"         j++; // we use this so the if doesn't get reduced to a series of bcsel\n"
"         if (j != 5)\n"
"           colour = vec4(1.0, 0.0, 0.0, 1.0);\n"
"      } else {\n"
"        colour = vec4(1.0, 0.0, 0.0, 1.0);\n"
"      }\n"
"      break;\n"
"    } else {\n"
"      if (i != 1) {\n"
"         j++;\n"
"      } else {\n"
"         colour = vec4(0.0, 1.0, 0.0, 1.0);\n"
"      }\n"
"    }\n"
"  } while (a == 1);\n"
"\n"
"  gl_FrontColor = colour;\n"
"}\n"
"\n"},
{SR_FS,47,
"void main()\n"
"{\n"
"  gl_FragColor = gl_Color;\n"
"}\n"
"\n"},
{SR_VS,10,
"uniform int loop_count;\n"
"\n"
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"\n"
"  vec4 colour = vec4(1.0, 1.0, 1.0, 1.0);\n"
"  vec4 colour2 = vec4(0.0, 0.0, 0.0, 1.0);\n"
"  for (int i = 0; i < loop_count; i++) {\n"
"\n"
"     if (i > 1) {\n"
"        colour = vec4(1.0, 0.0, 0.0, 1.0);\n"
"     }\n"
"\n"
"     if (i <= 1) {\n"
"        colour = vec4(0.0, 1.0, 0.0, 1.0);\n"
"     } else {\n"
"        break;\n"
"     }\n"
"\n"
"     /* Unreachable break */\n"
"     if (i > 2) {\n"
"        break;\n"
"     }\n"
"\n"
"     /* This should be unreachable */\n"
"     if (i >= 2) {\n"
"        colour2 = vec4(0.0, 1.0, 0.0, 1.0);\n"
"     }\n"
"  }\n"
"\n"
"  gl_FrontColor = colour + colour2;\n"
"}\n"
"\n"},
{SR_FS,45,
"void main()\n"
"{\n"
"  gl_FragColor = gl_Color;\n"
"}\n"
"\n"},
{SR_VS,10,
"uniform int loop_count;\n"
"\n"
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"\n"
"  vec4 colour = vec4(1.0, 1.0, 1.0, 1.0);\n"
"  vec4 colour2 = vec4(0.0, 0.0, 0.0, 1.0);\n"
"  for (int i = 0; i < loop_count; i++) {\n"
"\n"
"     if (i > 1) {\n"
"        colour = vec4(1.0, 0.0, 0.0, 1.0);\n"
"     }\n"
"\n"
"     if (i > 1) {\n"
"        break;\n"
"     }\n"
"\n"
"     colour = vec4(0.0, 1.0, 0.0, 1.0);\n"
"\n"
"     /* This should be unreachable */\n"
"     if (i >= 2) {\n"
"        colour2 = vec4(0.0, 1.0, 0.0, 1.0);\n"
"     }\n"
"  }\n"
"\n"
"  gl_FrontColor = colour + colour2;\n"
"}\n"
"\n"},
{SR_FS,40,
"void main()\n"
"{\n"
"  gl_FragColor = gl_Color;\n"
"}\n"
"\n"},
{SR_VS,9,
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"\n"
"  vec4 colour = vec4(1.0, 0.0, 0.0, 1.0);\n"
"\n"
"  int i = 0;\n"
"  int j = 0; // we use this so the if doesn't get reduced to a series of bcsel\n"
"  do {\n"
"    i++;\n"
"    j++;\n"
"\n"
"    if (i >= 3) {\n"
"      if (i == 3) {\n"
"         colour = vec4(0.0, 1.0, 0.0, 1.0);\n"
"         j++;\n"
"         if (j != 6)\n"
"           colour = vec4(1.0, 0.0, 1.0, 1.0);\n"
"      } else {\n"
"        colour = vec4(1.0, 1.0, 0.0, 1.0);\n"
"      }\n"
"      break;\n"
"    } else {\n"
"      if (i != 1) {\n"
"         j++;\n"
"      }\n"
"    }\n"
"\n"
"   if (i >= 5) {\n"
"      j++; // unreachable\n"
"      break;\n"
"    } else {\n"
"      if (i != 1) {\n"
"         j++;\n"
"      }\n"
"    }\n"
"  } while (i < 4);\n"
"\n"
"  gl_FrontColor = colour;\n"
"}\n"
"\n"},
{SR_FS,51,
"void main()\n"
"{\n"
"  gl_FragColor = gl_Color;\n"
"}\n"
"\n"},
{SR_VS,7,
"uniform int loop_count;\n"
"\n"
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"\n"
"  vec4 colour = vec4(1.0, 1.0, 1.0, 1.0);\n"
"  vec4 colour2 = vec4(0.0, 0.0, 0.0, 0.0);\n"
"  for (int i = 0; i < 3; i++) {\n"
"     if (i <= 1) {\n"
"        colour = vec4(0.0, 1.0, 0.0, 1.0);\n"
"     } else {\n"
"        break;\n"
"     }\n"
"\n"
"     /* This should be unreachable */\n"
"     if (i >= 2) {\n"
"        colour2 = vec4(1.0, 0.0, 0.0, 1.0);\n"
"     }\n"
"  }\n"
"\n"
"  gl_FrontColor = colour + colour2;\n"
"}\n"
"\n"},
{SR_FS,32,
"void main()\n"
"{\n"
"  gl_FragColor = gl_Color;\n"
"}\n"
"\n"},
{SR_VS,8,
"uniform int loop_count;\n"
"\n"
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"\n"
"  vec4 colour = vec4(1.0, 0.0, 0.0, 1.0);\n"
"\n"
"  for (int i = 0; i < loop_count; i++) {\n"
"     colour = vec4(0.0, 1.0, 0.0, 1.0);\n"
"\n"
"     if (i == 0)\n"
"        break;\n"
"  }\n"
"\n"
"  gl_FrontColor = colour;\n"
"}\n"
"\n"},
{SR_FS,27,
"void main()\n"
"{\n"
"  gl_FragColor = gl_Color;\n"
"}\n"
"\n"},
{SR_VS,8,
"uniform int loop_count;\n"
"\n"
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"  /* Make sure that loop_count uniform live */\n"
"  float one = float(loop_count == 0 || loop_count == 1);\n"
"\n"
"  vec4 colour = vec4(0.0, 1.0, 0.0, one);\n"
"\n"
"  for (int i = 0; i < 0; i++) {\n"
"     colour = vec4(1.0, 0.0, 0.0, 1.0);\n"
"\n"
"     if (i == loop_count)\n"
"        break;\n"
"  }\n"
"\n"
"  gl_FrontColor = colour;\n"
"}\n"
"\n"},
{SR_FS,29,
"void main()\n"
"{\n"
"  gl_FragColor = gl_Color;\n"
"}\n"
"\n"},
{SR_VS,5,
"uniform mat2 m[2];\n"
"\n"
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"\tmat2 a = m[0];\n"
"\tmat2 b = m[1];\n"
"\n"
"\t/* Prevent copy propagation to help trigger the driver bug on\n"
"\t * the assignment above.\n"
"\t */\n"
"\ta[0].x = 0.0;\n"
"\tb[0].x = 0.0;\n"
"\n"
"\tif (a == b)\n"
"\t\tgl_FrontColor = vec4(0.0, 1.0, 0.0, 0.0);\n"
"\telse\n"
"\t\tgl_FrontColor = vec4(1.0, 0.0, 0.0, 0.0);\n"
"}\n"
"\n"},
{SR_FS,26,
"void main() {\n"
"\tgl_FragColor = gl_Color;\n"
"}\n"
"\n"},
{SR_VS,5,
"uniform struct {\n"
"\tmat2 a, b;\n"
"} m;\n"
"\n"
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"\tmat2 a = m.a;\n"
"\tmat2 b = m.b;\n"
"\n"
"\t/* Prevent copy propagation to help trigger the driver bug on\n"
"\t * the assignment above.\n"
"\t */\n"
"\ta[0].x = 0.0;\n"
"\tb[0].x = 0.0;\n"
"\n"
"\tif (a == b)\n"
"\t\tgl_FrontColor = vec4(0.0, 1.0, 0.0, 0.0);\n"
"\telse\n"
"\t\tgl_FrontColor = vec4(1.0, 0.0, 0.0, 0.0);\n"
"}\n"
"\n"},
{SR_FS,28,
"void main() {\n"
"\tgl_FragColor = gl_Color;\n"
"}\n"
"\n"},
{SR_VS,8,
"uniform int a;\n"
"uniform int b;\n"
"uniform int c;\n"
"\n"
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"\n"
"  vec4 colour = vec4(1.0, 0.0, 0.0, 1.0);\n"
"\n"
"  if (a == 1) {\n"
"    if (b == 1) {\n"
"       colour = vec4(0.0, 1.0, 0.0, 1.0);\n"
"    }\n"
"\n"
"    if (c == 1) {\n"
"      gl_FrontColor = vec4(1.0, 1.0, 0.0, 1.0);\n"
"      return;\n"
"    }\n"
"  }\n"
"\n"
"  gl_FrontColor = colour;\n"
"}\n"
"\n"},
{SR_FS,33,
"void main()\n"
"{\n"
"  gl_FragColor = gl_Color;\n"
"}\n"
"\n"},
{SR_VS,8,
"uniform int a;\n"
"uniform int b;\n"
"uniform int c;\n"
"\n"
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"\n"
"  vec4 colour = vec4(1.0, 0.0, 0.0, 1.0);\n"
"\n"
"  if (a == 1) {\n"
"    if (c == 1) {\n"
"      gl_FrontColor = vec4(1.0, 1.0, 0.0, 1.0);\n"
"      return;\n"
"    }\n"
"\n"
"    if (b == 1) {\n"
"       colour = vec4(0.0, 1.0, 0.0, 1.0);\n"
"    }\n"
"  }\n"
"\n"
"  gl_FrontColor = colour;\n"
"}\n"
"\n"},
{SR_FS,33,
"void main()\n"
"{\n"
"  gl_FragColor = gl_Color;\n"
"}\n"
"\n"},
{SR_VS,8,
"uniform int a;\n"
"uniform int b;\n"
"uniform int c;\n"
"\n"
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"\n"
"  vec4 colour = vec4(1.0, 0.0, 0.0, 1.0);\n"
"\n"
"  if (a == 1) {\n"
"    do {\n"
"       colour = vec4(0.0, 1.0, 0.0, 1.0);\n"
"    } while (b == 2);\n"
"\n"
"    while (c == 1) {\n"
"      gl_FrontColor = vec4(1.0, 1.0, 0.0, 1.0);\n"
"      return;\n"
"    }\n"
"  }\n"
"\n"
"  gl_FrontColor = colour;\n"
"}\n"
"\n"},
{SR_FS,33,
"void main()\n"
"{\n"
"  gl_FragColor = gl_Color;\n"
"}\n"
"\n"},
{SR_VS,8,
"uniform int a;\n"
"uniform int b;\n"
"uniform int c;\n"
"\n"
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"\n"
"  vec4 colour = vec4(1.0, 0.0, 0.0, 1.0);\n"
"\n"
"  if (a == 1) {\n"
"    while (c == 1) {\n"
"      gl_FrontColor = vec4(1.0, 1.0, 0.0, 1.0);\n"
"      return;\n"
"    }\n"
"\n"
"    do {\n"
"       colour = vec4(0.0, 1.0, 0.0, 1.0);\n"
"    } while (b == 2);\n"
"  }\n"
"\n"
"  gl_FrontColor = colour;\n"
"}\n"
"\n"},
{SR_FS,33,
"void main()\n"
"{\n"
"  gl_FragColor = gl_Color;\n"
"}\n"
"\n"},
{SR_VS,8,
"bvec4 foo(vec4 x, vec4 y)\n"
"{\n"
"  return notEqual(x, y);\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"  gl_FrontColor = vec4(foo(vec4(0.0, 0.0, 1.0, 1.0),\n"
"                           vec4(0.0, 1.0, 1.0, 0.0)));\n"
"}\n"
"\n"},
{SR_FS,21,
"void main()\n"
"{\n"
"  gl_FragColor = gl_Color;\n"
"}\n"
"\n"},
{SR_VS,5,
"uniform vec4 a;\n"
"varying vec4 color;\n"
"attribute vec4 piglit_vertex;\n"
"\n"
"void main()\n"
"{\n"
"    color = vec4(notEqual(piglit_vertex.w * a, vec4(0)));\n"
"    gl_Position = piglit_vertex;\n"
"}\n"
"\n"},
{SR_FS,16,
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"    gl_FragColor = color;\n"
"}\n"
"\n"},
{SR_VS,5,
"uniform vec4 v;\n"
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"\tcolor = 0.25 + clamp(exp2(v), 0.0, 1.0) * 0.5;\n"
"}\n"
"\n"},
{SR_FS,15,
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"\tgl_FragColor = color;\n"
"}\n"
"\n"},
{SR_VS,5,
"uniform vec4 x;\n"
"uniform vec4 y;\n"
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"\tcolor = 0.25 + clamp(pow(x, y), 0.0, 1.0) * 0.5;\n"
"}\n"
"\n"},
{SR_FS,16,
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"\tgl_FragColor = color;\n"
"}\n"
"\n"},
{SR_VS,5,
"uniform vec4 v;\n"
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"\tcolor = 0.25 + clamp(sqrt(v), 0.0, 1.0) * 0.5;\n"
"}\n"
"\n"},
{SR_FS,15,
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"\tgl_FragColor = color;\n"
"}\n"
"\n"},
{SR_VS,5,
"void main()\n"
"{\n"
"\tvec4 color = vec4(0.1, 0.2, 0.3, 0.4);\n"
"\tcolor.xy.x = 1.0;\n"
"\tgl_FrontColor = color;\n"
"\tgl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,14,
"void main()\n"
"{\n"
"\tgl_FragColor = gl_Color;\n"
"}\n"
"\n"},
{SR_VS,5,
"void main()\n"
"{\n"
"\tvec4 color = vec4(0.1, 0.2, 0.3, 0.4);\n"
"\tcolor.wxz.yx.y = 1.0;\n"
"\tgl_FrontColor = color;\n"
"\tgl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,14,
"void main()\n"
"{\n"
"\tgl_FragColor = gl_Color;\n"
"}\n"
"\n"},
{SR_VS,5,
"void main()\n"
"{\n"
"\tvec4 color = vec4(0.0, 0.0, 0.0, 0.0);\n"
"\tcolor.xzy.z = 1.0;\n"
"\tgl_FrontColor = color;\n"
"\tgl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,14,
"void main()\n"
"{\n"
"\tgl_FragColor = gl_Color;\n"
"}\n"
"\n"},
{SR_VS,5,
"void main()\n"
"{\n"
"\tvec4 color = vec4(1.0, 0.0, 0.0, 0.0);\n"
"\tgl_FrontColor = color.yzx.xzxx;\n"
"\tgl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,13,
"void main()\n"
"{\n"
"\tgl_FragColor = gl_Color;\n"
"}\n"
"\n"},
{SR_VS,5,
"uniform int early_return;\n"
"varying vec2 foo1;\n"
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"\tfoo1 = vec2(0.5);\n"
"\tif (early_return != 0)\n"
"\t\treturn;\n"
"\tfoo1 = vec2(0.2);\n"
"}\n"
"\n"},
{SR_FS,17,
"varying vec2 foo1;\n"
"void main()\n"
"{\n"
"\tgl_FragColor.xy = foo1;\n"
"\tgl_FragColor.zw = foo1;\n"
"}\n"
"\n"},
{SR_VS,5,
"uniform int early_return;\n"
"varying vec3 foo1;\n"
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"\tfoo1 = vec3(0.5);\n"
"\tif (early_return != 0)\n"
"\t\treturn;\n"
"\tfoo1 = vec3(0.2);\n"
"}\n"
"\n"},
{SR_FS,17,
"varying vec3 foo1;\n"
"void main()\n"
"{\n"
"\tgl_FragColor = foo1.xyzx;\n"
"}\n"
"\n"},
{SR_VS,5,
"uniform int early_return;\n"
"varying vec4 foo1;\n"
"void main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"\tfoo1 = vec4(0.5);\n"
"\tif (early_return != 0)\n"
"\t\treturn;\n"
"\tfoo1 = vec4(0.2);\n"
"}\n"
"\n"},
{SR_FS,17,
"varying vec4 foo1;\n"
"void main()\n"
"{\n"
"\tgl_FragColor = foo1;\n"
"}\n"
"\n"},
{SR_VS,8,
"#define RED vec4(1.0, 0.0, 0.0, 0.0)\n"
"#define GREEN vec4(0.0, 1.0, 0.0, 0.0)\n"
"\n"
"uniform int three;\n"
"uniform float five;\n"
"\n"
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"\n"
"  vec4 v = vec4(1.0, 2.0, 3.0, 4.0);\n"
"\n"
"  // If the bug is present, constant propagation will see a scalar on\n"
"  // the RHS, and incorrectly conclude that therefore only v.x is\n"
"  // killed.\n"
"  v[three] = five;\n"
"\n"
"  vec4 color = v.a == 5.0 ? GREEN : RED;\n"
"\n"
"  // If the bug is present, then index and new_value will be optimized\n"
"  // away, making shader_runner fail to assign uniforms.  To avoid\n"
"  // that, multiply color by (five - three - 1.0).\n"
"  gl_FrontColor = color * (five - float(three) - 1.0);\n"
"}\n"
"\n"},
{SR_FS,34,
"void main()\n"
"{\n"
"  gl_FragColor = gl_Color;\n"
"}\n"
"\n"},
{SR_VS,10,
"uniform vec4 data;\n"
"\n"
"varying vec4 a[3];\n"
"varying float b;\n"
"\n"
"void main()\n"
"{\n"
"  a[0] = data.xxxx;\n"
"  a[1] = data.yyyy;\n"
"  a[2] = data.zzzz;\n"
"  b = data.w;\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,25,
"varying vec4 a[3];\n"
"varying float b;\n"
"\n"
"void main()\n"
"{\n"
"  gl_FragColor = vec4(a[0].x, a[2].x, b, 1.0);\n"
"}\n"
"\n"},
{SR_FS,5,
"void main()\n"
"{\n"
"\tgl_FragColor = vec4(1.0);\n"
"\tgl_FragColor.x = 0.5;\n"
"\tgl_FragColor.z = 0.25;\n"
"}\n"
"\n"
"\n"},
{SR_PASSVS,6,
"\n"},
{SR_FS,8,
"void main()\n"
"{\n"
"\tif (gl_FragCoord.x < 10.0)\n"
"\t\tdiscard;\n"
"}\n"
"\n"},
{SR_VS,8,
"void f();\n"
"void main()\n"
"{\n"
"  f();\n"
"}\n"
"\n"},
{SR_VS,15,
"void g()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"  gl_FrontColor = vec4(0.0, 1.0, 0.0, 1.0);\n"
"}\n"
"void f()\n"
"{\n"
"  g();\n"
"}\n"
"\n"},
{SR_FS,26,
"void main()\n"
"{\n"
"  gl_FragColor = gl_Color;\n"
"}\n"
"\n"},
{SR_VS,5,
"void main()\n"
"{\n"
"\tgl_FrontColor = vec4(0.3);\n"
"}\n"
"\n"
"\n"},
{SR_FS,12,
"void main()\n"
"{\n"
"\tgl_FragColor = gl_Color;\n"
"}\n"
"\n"},
{SR_VS,5,
"const float global_constant = 1.0;\n"
"\n"
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_VS,13,
"const float global_constant = 2.0;\n"
"\n"
"/* This works around a bug in Apple's GLSL compiler.  Their compiler won't allow\n"
" * a shader doesn't have any executable code.\n"
" */\n"
"void apple_work_around2() {}\n"
"\n"},
{SR_VS,5,
"float global_variable = 2.0;\n"
"\n"
"/* This works around a bug in Apple's GLSL compiler.  Their compiler won't allow\n"
" * a shader doesn't have any executable code.\n"
" */\n"
"void apple_work_around2() {}\n"
"\n"},
{SR_VS,13,
"float global_variable = 2.0;\n"
"\n"
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_VS,5,
"uniform float angle;\n"
"uniform float scale;\n"
"float global_variable = sin(angle) * scale;\n"
"\n"
"/* This works around a bug in Apple's GLSL compiler.  Their compiler won't allow\n"
" * a shader doesn't have any executable code.\n"
" */\n"
"void apple_work_around1() {}\n"
"\n"},
{SR_VS,15,
"uniform float angle;\n"
"uniform float scale;\n"
"float global_variable = sin(angle) * scale;\n"
"\n"
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_VS,5,
"float global_variable = 1.0;\n"
"\n"
"/* This works around a bug in Apple's GLSL compiler.  Their compiler won't allow\n"
" * a shader doesn't have any executable code.\n"
" */\n"
"void apple_work_around1() {}\n"
"\n"},
{SR_VS,13,
"float global_variable = 2.0;\n"
"\n"
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_VS,5,
"float global_variable;\n"
"\n"
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_VS,13,
"float global_variable = 1.0;\n"
"\n"
"/* This works around a bug in Apple's GLSL compiler.  Their compiler won't allow\n"
" * a shader doesn't have any executable code.\n"
" */\n"
"void apple_work_around1() {}\n"
"\n"},
{SR_VS,21,
"float global_variable = 2.0;\n"
"\n"
"/* This works around a bug in Apple's GLSL compiler.  Their compiler won't allow\n"
" * a shader doesn't have any executable code.\n"
" */\n"
"void apple_work_around2() {}\n"
"\n"},
{SR_VS,5,
"uniform float angle;\n"
"uniform float scale;\n"
"float global_variable = cos(angle) * scale;\n"
"\n"
"float function(void)\n"
"{\n"
"  return global_variable;\n"
"}\n"
"\n"},
{SR_VS,15,
"uniform float angle;\n"
"uniform float scale;\n"
"float global_variable = scale * sin(angle);\n"
"\n"
"float function(void);\n"
"\n"
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex * global_variable * function();\n"
"}\n"
"\n"},
{SR_VS,5,
"float global_variable;\n"
"\n"
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_VS,13,
"float global_variable = 1.0;\n"
"\n"
"/* This works around a bug in Apple's GLSL compiler.  Their compiler won't allow\n"
" * a shader doesn't have any executable code.\n"
" */\n"
"void apple_work_around1() {}\n"
"\n"},
{SR_FS,5,
"struct s {\n"
"\tvec4 v[1];\n"
"\tfloat f;\n"
"};\n"
"s a[1];\n"
"\n"
"void main()\n"
"{\n"
"\ta[0].v[0] = vec4(0.0, 0.0, 0.0, 0.0);\n"
"\ta[0].f = 1.0;\n"
"\tgl_FragColor = vec4(0.0, a[0].f, 0.0 ,1.0);\n"
"}\n"
"\n"},
{SR_VS,5,
"void main() { gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; }\n"
"\n"},
{SR_FS,8,
"/* From page 38 (page 44 of the PDF) of the GLSL 1.10 spec:\n"
" *\n"
" *     \"A shader can redefine built-in functions. If a built-in function is\n"
" *     redeclared in a shader (i.e. a prototype is visible) before a call to\n"
" *     it, then the linker will only attempt to resolve that call within the\n"
" *     set shaders that are linked with it.\"\n"
" */\n"
"void main()\n"
"{\n"
"  gl_FragColor = abs(vec4(0.0, -1.0, 0.0, 1.0));\n"
"}\n"
"\n"
"vec4 abs(vec4 unused)\n"
"{\n"
"  return vec4(1.0, 0.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_VS,5,
"void main() { gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; }\n"
"\n"},
{SR_FS,8,
"/* From page 38 (page 44 of the PDF) of the GLSL 1.10 spec:\n"
" *\n"
" *     \"A shader can redefine built-in functions. If a built-in function is\n"
" *     redeclared in a shader (i.e. a prototype is visible) before a call to\n"
" *     it, then the linker will only attempt to resolve that call within the\n"
" *     set shaders that are linked with it.\"\n"
" */\n"
"vec4 abs(vec4 x)\n"
"{\n"
"  /* Return a swizzle of x instead of a constant value because we don't want\n"
"   * the compiler to optimize away all uses of the uniform.  This will make\n"
"   * the test fail because shader_runner won't be able to glGetUnformLocation\n"
"   * on the uniform.\n"
"   */\n"
"  return x.yxzw;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"  gl_FragColor = abs(vec4(1.0, 0.0, 0.0, 1.0));\n"
"}\n"
"\n"},
{SR_VS,5,
"void main() { gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; }\n"
"\n"},
{SR_FS,8,
"/* From page 38 (page 44 of the PDF) of the GLSL 1.10 spec:\n"
" *\n"
" *     \"A shader can redefine built-in functions. If a built-in function is\n"
" *     redeclared in a shader (i.e. a prototype is visible) before a call to\n"
" *     it, then the linker will only attempt to resolve that call within the\n"
" *     set shaders that are linked with it.\"\n"
" */\n"
"void main()\n"
"{\n"
"  gl_FragColor = abs(vec4(0.0, -1.0, 0.0, 1.0));\n"
"}\n"
"\n"},
{SR_FS,21,
"vec4 abs(vec4 unused)\n"
"{\n"
"  return vec4(1.0, 0.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_VS,5,
"void main() { gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; }\n"
"\n"},
{SR_FS,8,
"/* From page 38 (page 44 of the PDF) of the GLSL 1.10 spec:\n"
" *\n"
" *     \"A shader can redefine built-in functions. If a built-in function is\n"
" *     redeclared in a shader (i.e. a prototype is visible) before a call to\n"
" *     it, then the linker will only attempt to resolve that call within the\n"
" *     set shaders that are linked with it.\"\n"
" */\n"
"vec4 abs(vec4 x);\n"
"\n"
"void main()\n"
"{\n"
"  gl_FragColor = abs(vec4(1.0, 0.0, 0.0, 1.0));\n"
"}\n"
"\n"},
{SR_FS,23,
"vec4 abs(vec4 x)\n"
"{\n"
"  /* Return a swizzle of x instead of a constant value because we don't want\n"
"   * the compiler to optimize away all uses of the uniform.  This will make\n"
"   * the test fail because shader_runner won't be able to glGetUnformLocation\n"
"   * on the uniform.\n"
"   */\n"
"  return x.yxzw;\n"
"}\n"
"\n"},
{SR_VS,5,
"void main() { gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; }\n"
"\n"},
{SR_FS,8,
"/* From page 38 (page 44 of the PDF) of the GLSL 1.10 spec:\n"
" *\n"
" *     \"A shader can redefine built-in functions. If a built-in function is\n"
" *     redeclared in a shader (i.e. a prototype is visible) before a call to\n"
" *     it, then the linker will only attempt to resolve that call within the\n"
" *     set shaders that are linked with it.\"\n"
" */\n"
"float func(float x)\n"
"{\n"
"  return abs(x);\n"
"}\n"
"\n"
"float abs(float x)\n"
"{\n"
"  return x;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"  /* The call to abs in func should bind to the built-in version, but\n"
"   * the call to abs here, which follows the override, should not.\n"
"   */\n"
"  gl_FragColor = (func(-1.0) == abs(-1.0))\n"
"\t\t  ? vec4(1.0, 0.0, 0.0, 1.0)\n"
"\t\t  : vec4(0.0, 1.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_VS,5,
"void main() { gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; }\n"
"\n"},
{SR_FS,8,
"/* From page 38 (page 44 of the PDF) of the GLSL 1.10 spec:\n"
" *\n"
" *     \"A shader can redefine built-in functions. If a built-in function is\n"
" *     redeclared in a shader (i.e. a prototype is visible) before a call to\n"
" *     it, then the linker will only attempt to resolve that call within the\n"
" *     set shaders that are linked with it.\"\n"
" */\n"
"void main()\n"
"{\n"
"  gl_FragColor = abs(vec4(0.0, -1.0, 0.0, 1.0));\n"
"}\n"
"\n"
"vec4 abs(vec4 unused);\n"
"\n"},
{SR_FS,23,
"vec4 abs(vec4 unused)\n"
"{\n"
"  return vec4(1.0, 0.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_VS,5,
"void main() { gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; }\n"
"\n"},
{SR_FS,8,
"/* From page 38 (page 44 of the PDF) of the GLSL 1.10 spec:\n"
" *\n"
" *     \"A shader can redefine built-in functions. If a built-in function is\n"
" *     redeclared in a shader (i.e. a prototype is visible) before a call to\n"
" *     it, then the linker will only attempt to resolve that call within the\n"
" *     set shaders that are linked with it.\"\n"
" */\n"
"float func(float);\n"
"\n"
"float abs(float x)\n"
"{\n"
"  return x;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"  /* The call to abs in func should bind to the built-in version, but\n"
"   * the call to abs here, which follows the override, should not.\n"
"   */\n"
"  gl_FragColor = (func(-1.0) == abs(-1.0))\n"
"\t\t  ? vec4(1.0, 0.0, 0.0, 1.0)\n"
"\t\t  : vec4(0.0, 1.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_FS,33,
"float func(float x)\n"
"{\n"
"  return abs(x);\n"
"}\n"
"\n"},
{SR_VS,5,
"void main() { gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; }\n"
"\n"},
{SR_FS,8,
"/* From page 38 (page 44 of the PDF) of the GLSL 1.10 spec:\n"
" *\n"
" *     \"A shader can redefine built-in functions. If a built-in function is\n"
" *     redeclared in a shader (i.e. a prototype is visible) before a call to\n"
" *     it, then the linker will only attempt to resolve that call within the\n"
" *     set shaders that are linked with it.\"\n"
" */\n"
"float func(float);\n"
"\n"
"float abs(float x)\n"
"{\n"
"  return x;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"  /* The call to abs in func should bind to the built-in version, but\n"
"   * the call to abs here, which follows the override, should not.\n"
"   */\n"
"  gl_FragColor = (func(-1.0) != abs(-1.0))\n"
"\t\t  ? vec4(1.0, 0.0, 0.0, 1.0)\n"
"\t\t  : vec4(0.0, 1.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_FS,33,
"float abs(float);\n"
"\n"
"float func(float x)\n"
"{\n"
"  return abs(x);\n"
"}\n"
"\n"},
{SR_VS,5,
"void main() { gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; }\n"
"\n"},
{SR_FS,8,
"/* From page 38 (page 44 of the PDF) of the GLSL 1.10 spec:\n"
" *\n"
" *     \"A shader can redefine built-in functions. If a built-in function is\n"
" *     redeclared in a shader (i.e. a prototype is visible) before a call to\n"
" *     it, then the linker will only attempt to resolve that call within the\n"
" *     set shaders that are linked with it.\"\n"
" */\n"
"uniform vec4 v;\n"
"\n"
"void main()\n"
"{\n"
"  gl_FragColor = abs(v);\n"
"}\n"
"\n"
"vec4 abs(vec4 unused)\n"
"{\n"
"  return vec4(1.0, 0.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_VS,5,
"void main() { gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; }\n"
"\n"},
{SR_FS,8,
"/* From page 38 (page 44 of the PDF) of the GLSL 1.10 spec:\n"
" *\n"
" *     \"A shader can redefine built-in functions. If a built-in function is\n"
" *     redeclared in a shader (i.e. a prototype is visible) before a call to\n"
" *     it, then the linker will only attempt to resolve that call within the\n"
" *     set shaders that are linked with it.\"\n"
" */\n"
"uniform vec4 v;\n"
"\n"
"vec4 abs(vec4 x)\n"
"{\n"
"  /* Return a swizzle of x instead of a constant value because we don't want\n"
"   * the compiler to optimize away all uses of the uniform.  This will make\n"
"   * the test fail because shader_runner won't be able to glGetUnformLocation\n"
"   * on the uniform.\n"
"   */\n"
"  return x.yxzw;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"  gl_FragColor = abs(v);\n"
"}\n"
"\n"},
{SR_VS,5,
"void main() { gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; }\n"
"\n"},
{SR_FS,8,
"/* From page 38 (page 44 of the PDF) of the GLSL 1.10 spec:\n"
" *\n"
" *     \"A shader can redefine built-in functions. If a built-in function is\n"
" *     redeclared in a shader (i.e. a prototype is visible) before a call to\n"
" *     it, then the linker will only attempt to resolve that call within the\n"
" *     set shaders that are linked with it.\"\n"
" */\n"
"uniform vec4 v;\n"
"\n"
"void main()\n"
"{\n"
"  gl_FragColor = abs(v);\n"
"}\n"
"\n"},
{SR_FS,23,
"vec4 abs(vec4 unused)\n"
"{\n"
"  return vec4(1.0, 0.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_VS,5,
"void main() { gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; }\n"
"\n"},
{SR_FS,8,
"/* From page 38 (page 44 of the PDF) of the GLSL 1.10 spec:\n"
" *\n"
" *     \"A shader can redefine built-in functions. If a built-in function is\n"
" *     redeclared in a shader (i.e. a prototype is visible) before a call to\n"
" *     it, then the linker will only attempt to resolve that call within the\n"
" *     set shaders that are linked with it.\"\n"
" */\n"
"uniform vec4 v;\n"
"vec4 abs(vec4 x);\n"
"\n"
"void main()\n"
"{\n"
"  gl_FragColor = abs(v);\n"
"}\n"
"\n"},
{SR_FS,24,
"vec4 abs(vec4 x)\n"
"{\n"
"  /* Return a swizzle of x instead of a constant value because we don't want\n"
"   * the compiler to optimize away all uses of the uniform.  This will make\n"
"   * the test fail because shader_runner won't be able to glGetUnformLocation\n"
"   * on the uniform.\n"
"   */\n"
"  return x.yxzw;\n"
"}\n"
"\n"},
{SR_VS,5,
"void main() { gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; }\n"
"\n"},
{SR_FS,8,
"/* From page 38 (page 44 of the PDF) of the GLSL 1.10 spec:\n"
" *\n"
" *     \"A shader can redefine built-in functions. If a built-in function is\n"
" *     redeclared in a shader (i.e. a prototype is visible) before a call to\n"
" *     it, then the linker will only attempt to resolve that call within the\n"
" *     set shaders that are linked with it.\"\n"
" */\n"
"uniform float f;\n"
"\n"
"float func(float x)\n"
"{\n"
"  return abs(x);\n"
"}\n"
"\n"
"float abs(float x)\n"
"{\n"
"  return x;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"  /* The call to abs in func should bind to the built-in version, but\n"
"   * the call to abs here, which follows the override, should not.\n"
"   */\n"
"  gl_FragColor = (func(f) == abs(f))\n"
"\t\t  ? vec4(1.0, 0.0, 0.0, 1.0)\n"
"\t\t  : vec4(0.0, 1.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_VS,5,
"void main() { gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; }\n"
"\n"},
{SR_FS,8,
"/* From page 38 (page 44 of the PDF) of the GLSL 1.10 spec:\n"
" *\n"
" *     \"A shader can redefine built-in functions. If a built-in function is\n"
" *     redeclared in a shader (i.e. a prototype is visible) before a call to\n"
" *     it, then the linker will only attempt to resolve that call within the\n"
" *     set shaders that are linked with it.\"\n"
" */\n"
"uniform vec4 v;\n"
"\n"
"void main()\n"
"{\n"
"  gl_FragColor = abs(v);\n"
"}\n"
"\n"
"vec4 abs(vec4 unused);\n"
"\n"},
{SR_FS,25,
"vec4 abs(vec4 unused)\n"
"{\n"
"  return vec4(1.0, 0.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_VS,5,
"void main() { gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; }\n"
"\n"},
{SR_FS,8,
"/* From page 38 (page 44 of the PDF) of the GLSL 1.10 spec:\n"
" *\n"
" *     \"A shader can redefine built-in functions. If a built-in function is\n"
" *     redeclared in a shader (i.e. a prototype is visible) before a call to\n"
" *     it, then the linker will only attempt to resolve that call within the\n"
" *     set shaders that are linked with it.\"\n"
" */\n"
"uniform float f;\n"
"\n"
"float func(float);\n"
"\n"
"float abs(float x)\n"
"{\n"
"  return x;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"  /* The call to abs in func should bind to the built-in version, but\n"
"   * the call to abs here, which follows the override, should not.\n"
"   */\n"
"  gl_FragColor = (func(f) == abs(f))\n"
"\t\t  ? vec4(1.0, 0.0, 0.0, 1.0)\n"
"\t\t  : vec4(0.0, 1.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_FS,35,
"float func(float x)\n"
"{\n"
"  return abs(x);\n"
"}\n"
"\n"},
{SR_VS,5,
"void main() { gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; }\n"
"\n"},
{SR_FS,8,
"/* From page 38 (page 44 of the PDF) of the GLSL 1.10 spec:\n"
" *\n"
" *     \"A shader can redefine built-in functions. If a built-in function is\n"
" *     redeclared in a shader (i.e. a prototype is visible) before a call to\n"
" *     it, then the linker will only attempt to resolve that call within the\n"
" *     set shaders that are linked with it.\"\n"
" */\n"
"uniform float f;\n"
"\n"
"float func(float);\n"
"\n"
"float abs(float x)\n"
"{\n"
"  return x;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"  /* The call to abs in func should bind to the built-in version, but\n"
"   * the call to abs here, which follows the override, should not.\n"
"   */\n"
"  gl_FragColor = (func(f) != abs(f))\n"
"\t\t  ? vec4(1.0, 0.0, 0.0, 1.0)\n"
"\t\t  : vec4(0.0, 1.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_FS,35,
"float abs(float);\n"
"\n"
"float func(float x)\n"
"{\n"
"  return abs(x);\n"
"}\n"
"\n"},
{SR_VS,8,
"varying vec4 gl_TexCoord[4];\n"
"void main()\n"
"{\n"
"\tint i = 1;\n"
"\tgl_TexCoord[i] = vec4(0.5, 0, 0, 0);\n"
"\tgl_Position = ftransform();\n"
"}\n"
"\n"
"\n"},
{SR_FS,18,
"varying vec4 gl_TexCoord[4];\n"
"void main()\n"
"{\n"
"\tgl_FragColor = gl_TexCoord[1];\n"
"}\n"
"\n"
"\n"},
{SR_VS,13,
"uniform vec4 a; // a is vec3 in FS\n"
"\n"
"void main()\n"
"{\n"
"    gl_Position = vec4(0.0);\n"
"}\n"
"\n"},
{SR_FS,21,
"uniform vec3 a; // a is vec4 in VS\n"
"\n"
"void main()\n"
"{\n"
"}\n"
"\n"},
{SR_VS,5,
"varying vec4 foo;\n"
"void main()\n"
"{\n"
"\tgl_Position = ftransform();\n"
"}\n"
"\n"
"\n"},
{SR_FS,13,
"varying vec4 foo;\n"
"void main()\n"
"{\n"
"\tgl_FragColor = foo;\n"
"}\n"
"\n"
"\n"},
{SR_VS,5,
"varying vec4 foo;\n"
"void main()\n"
"{\n"
"\tfoo = gl_Color;\n"
"\tgl_Position = ftransform();\n"
"}\n"
"\n"
"\n"},
{SR_FS,14,
"varying vec4 bar;\n"
"void main()\n"
"{\n"
"\tgl_FragColor = bar;\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"#define FOO 1\n"
"void main()\n"
"{\n"
"\tvec4 r = vec4(0.0, 0.0, 0.0, 0.0);\n"
"#if FOO == 1\n"
"\tr.y = 1.0;\n"
"#elif FOO == 2\n"
"\tr.x = 1.0;\n"
"#else\n"
"\tr.z = 1.0;\n"
"#endif\n"
"\tgl_FragColor = r;\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"#define FOO 2\n"
"void main()\n"
"{\n"
"\tvec4 r = vec4(0.0, 0.0, 0.0, 0.0);\n"
"#if FOO == 1\n"
"\tr.y = 1.0;\n"
"#elif FOO == 2\n"
"\tr.x = 1.0;\n"
"#else\n"
"\tr.z = 1.0;\n"
"#endif\n"
"\tgl_FragColor = r;\n"
"}\n"
"\n"
"\n"},
{SR_FS,9,
"#extension GL_ARB_draw_buffers: enable\n"
"void main()\n"
"{\n"
"#if defined(GL_ARB_draw_buffers)\n"
"\tgl_FragData[0] = vec4(0.0, 1.0, 0.0, 0.0);\n"
"#else\n"
"\tgl_FragColor = vec4(1.0, 0.0, 0.0, 0.0);\n"
"#endif\n"
"}\n"
"\n"
"\n"},
{SR_FS,9,
"#extension GL_ARB_draw_buffers: enable\n"
"void main()\n"
"{\n"
"#if GL_ARB_draw_buffers == 1\n"
"\tgl_FragData[0] = vec4(0.0, 1.0, 0.0, 0.0);\n"
"#else\n"
"\tgl_FragColor = vec4(1.0, 0.0, 0.0, 0.0);\n"
"#endif\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"#define BAR(x) x\n"
"#if 0\n"
"#define FOO(x) BAR(x)\n"
"#else\n"
"#define FOO(x) (BAR(x) + BAR(x))\n"
"#endif\n"
"void main()\n"
"{\n"
"\tgl_FragColor = vec4(FOO(0.25));\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"void main()\n"
"{\n"
"\tgl_FragColor = vec4(0.0);\n"
"#if 0\n"
"\tgl_FragColor += vec4(0.5);\n"
"#else\n"
"\tgl_FragColor += vec4(0.3);\n"
"#endif\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"#if 1\n"
"#define FOO(x) x\n"
"#else\n"
"#define FOO(x) (0.5 * (x))\n"
"#endif\n"
"void main()\n"
"{\n"
"\tgl_FragColor = vec4(FOO(0.25));\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"#define BAR(x) x\n"
"#if 1\n"
"#define FOO(x) BAR(x)\n"
"#else\n"
"#define FOO(x) (BAR(x) + BAR(x))\n"
"#endif\n"
"void main()\n"
"{\n"
"\tgl_FragColor = vec4(FOO(0.25));\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"void main()\n"
"{\n"
"\tgl_FragColor = vec4(0.0);\n"
"#if 1\n"
"\tgl_FragColor += vec4(0.5);\n"
"#else\n"
"\tgl_FragColor += vec4(0.3);\n"
"#endif\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"#define FOO\n"
"void main()\n"
"{\n"
"\tgl_FragColor = vec4(0.0, 1.0, 0.0, 0.0);\n"
"#if defined(FOO)\n"
"\tgl_FragColor.y = 1.0;\n"
"#else\n"
"\tgl_FragColor.x = 1.0;\n"
"#endif\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"void main()\n"
"{\n"
"\tgl_FragColor = vec4(0.0);\n"
"#define SYMBOL 3\n"
"#if SYMBOL == 3\n"
"\tgl_FragColor += vec4(0.5);\n"
"#else\n"
"\tgl_FragColor += vec4(0.3);\n"
"#endif\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"#define FOO\n"
"void main()\n"
"{\n"
"\tgl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);\n"
"#ifdef FOO\n"
"\tgl_FragColor.y = 1.0;\n"
"#else\n"
"\tgl_FragColor.x = 1.0;\n"
"#endif\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"#define FOO\n"
"void main()\n"
"{\n"
"\tgl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);\n"
"#ifndef FOO\n"
"\tgl_FragColor.y = 1.0;\n"
"#else\n"
"\tgl_FragColor.x = 1.0;\n"
"#endif\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"#define FOO(x) \\\n"
"\t((x) + (x))\n"
"void main()\n"
"{\n"
"\tgl_FragColor = vec4(FOO(0.25));\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"#define FOO 1\n"
"#define BAR 0\n"
"void main()\n"
"{\n"
"\tvec4 r = vec4(0.0, 0.0, 0.0, 0.0);\n"
"#if FOO == 1\n"
"#if BAR == 1\n"
"\tr.x = 1.0;\n"
"#else\n"
"\tr.y = 1.0;\n"
"#endif\n"
"#else\n"
"\tr.z = 1.0;\n"
"#endif\n"
"\tgl_FragColor = r;\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"#define FOO 0\n"
"#define BAR 0\n"
"void main()\n"
"{\n"
"\tvec4 r = vec4(0.0, 0.0, 0.0, 0.0);\n"
"#if FOO == 1\n"
"\tr.z = 1.0;\n"
"#else\n"
"#if BAR == 1\n"
"\tr.x = 1.0;\n"
"#else\n"
"\tr.y = 1.0;\n"
"#endif\n"
"#endif\n"
"\tgl_FragColor = r;\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"#define FOO 0\n"
"#define BAR 2\n"
"void main()\n"
"{\n"
"\tvec4 r = vec4(0.0, 0.0, 0.0, 0.0);\n"
"#if FOO == 1\n"
"\tr.z = 1.0;\n"
"#else\n"
"#if BAR == 1\n"
"\tr.x = 1.0;\n"
"#elif BAR == 2\n"
"\tr.x = 1.0;\n"
"#else\n"
"\tr.y = 1.0;\n"
"#endif\n"
"#endif\n"
"\tgl_FragColor = r;\n"
"}\n"
"\n"
"\n"},
{SR_FS,5,
"#define FOO\n"
"void main()\n"
"{\n"
"\tgl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);\n"
"#if !defined(FOO)\n"
"\tgl_FragColor.y = 1.0;\n"
"#else\n"
"\tgl_FragColor.x = 1.0;\n"
"#endif\n"
"}\n"
"\n"
"\n"},
{SR_VS,6,
"void\n"
"main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"}\n"
"\n"
"\n"},
{SR_FS,14,
"\n"
"const int unused_array[1] = int[1](1);\n"
"\n"
"void\n"
"main()\n"
"{\n"
"\tgl_FragColor = vec4(0, 1, 0, 1);\n"
"}\n"
"\n"
"\n"},
{SR_FS,24,
"#version 120\n"
"void main(void)\n"
"{\n"
"\tgl_FragColor = vec4(1, 1, 1, 1);\n"
"}\n"
"\n"},
{SR_VS,21,
"#version 120\n"
"void main(void)\n"
"{\n"
"\tgl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
"\n"
"\tgl_ClipVertex = vec4(1.0, 0.0, 0.0, 0.0);\n"
"}\n"
"\n"},
{SR_FS,30,
"#version 120\n"
"void main(void)\n"
"{\n"
"\tgl_FragColor = vec4(1.0);\n"
"}\n"
"\n"},
{SR_VS,21,
"#version 120\n"
"void main(void)\n"
"{\n"
"\tgl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
"\n"
"\tgl_ClipVertex = vec4(-1.0, 0.0, 0.0, 0.0);\n"
"}\n"
"\n"},
{SR_FS,30,
"#version 120\n"
"void main(void)\n"
"{\n"
"\tgl_FragColor = vec4(1.0);\n"
"}\n"
"\n"},
{SR_VS,12,
"#version 120\n"
"void main(void)\n"
"{\n"
"\tgl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
"\n"
"\t// Transform gl_ClipVertex in an arbitrary way so that\n"
"\t// we can verify it is being used for clipping instead of\n"
"\t// gl_Position.\n"
"\tgl_ClipVertex = gl_Vertex * vec4(10.0, 10.0, 1.0, 1.0);\n"
"}\n"
"\n"},
{SR_FS,24,
"#version 120\n"
"void main(void)\n"
"{\n"
"\tgl_FragColor = vec4(1, 1, 1, 1);\n"
"}\n"
"\n"},
{SR_VS,21,
"#version 120\n"
"void main(void)\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"\n"
"\tgl_ClipVertex = gl_Position;\n"
"}\n"
"\n"},
{SR_FS,30,
"#version 120\n"
"void main(void)\n"
"{\n"
"\tgl_FragColor = vec4(1, 1, 1, 1);\n"
"}\n"
"\n"},
{SR_VS,10,
"#version 120\n"
"void main(void)\n"
"{\n"
"\tgl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
"\n"
"\tgl_ClipVertex = gl_Position;\n"
"}\n"
"\n"},
{SR_FS,19,
"#version 120\n"
"void main(void)\n"
"{\n"
"\tgl_FragColor = vec4(1, 1, 1, 1);\n"
"}\n"
"\n"},
{SR_VS,16,
"#version 120\n"
"void main(void)\n"
"{\n"
"\tgl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
"\n"
"\t// Transform gl_ClipVertex in an arbitrary way so that\n"
"\t// we can verify it is being used for clipping instead of\n"
"\t// gl_Position.  The x and y coordinates are multiplied by 5,\n"
"\t// and the homogeneous coordinate is multiplied by 0.5, so the\n"
"\t// net result should be that x and y are scaled by a factor of\n"
"\t// 10.\n"
"\tgl_ClipVertex = gl_Vertex * vec4(5.0, 5.0, 1.0, 0.5);\n"
"}\n"
"\n"},
{SR_FS,31,
"#version 120\n"
"void main(void)\n"
"{\n"
"\tgl_FragColor = vec4(1, 1, 1, 1);\n"
"}\n"
"\n"},
{SR_VS,7,
"#version 120\n"
"\n"
"attribute vec2 in_pos;\n"
"\n"
"uniform vec2 u_offset;\n"
"uniform float u_clipdist;\n"
"\n"
"void main(void)\n"
"{\n"
"\tgl_Position = gl_ModelViewProjectionMatrix * vec4(u_offset + in_pos, 0, 1);\n"
"\n"
"\tgl_ClipVertex = vec4(u_clipdist, 0.0, 0.0, 0.0);\n"
"}\n"
"\n"},
{SR_FS,22,
"#version 120\n"
"void main(void)\n"
"{\n"
"\tgl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_PASSVS,26,
"\n"},
{SR_FS,28,
"void qualifiers(in float a[4], out float b[4], inout float c[4], const in float d[4], float e[4])\n"
"{\n"
"\tb[0] = a[0];\n"
"\tc[0] += d[0];\n"
"\ta[0] += 1.0;\n"
"\te[0] += 1.0;\n"
"\n"
"\tb[1] = a[1];\n"
"\tc[1] += d[1];\n"
"\ta[1] += 1.0;\n"
"\te[1] += 1.0;\n"
"\n"
"\tb[2] = a[2];\n"
"\tc[2] += d[2];\n"
"\ta[2] += 1.0;\n"
"\te[2] += 1.0;\n"
"\n"
"\tb[3] = a[3];\n"
"\tc[3] += d[3];\n"
"\ta[3] += 1.0;\n"
"\te[3] += 1.0;\n"
"}\n"
"\n"
"void main (void)\n"
"{\n"
"\tfloat a[4];\n"
"\tfloat b[4];\n"
"\tfloat c[4];\n"
"\tfloat d[4];\n"
"\tfloat e[4];\n"
"\tfloat q = 0.0;\n"
"\tfloat q2 = 0.0;\n"
"\n"
"\ta[0] = 1.0;\n"
"\tb[0] = 2.0;\n"
"\tc[0] = 3.0;\n"
"\td[0] = 4.0;\n"
"\te[0] = 1.0;\n"
"\n"
"\ta[1] = 1.0;\n"
"\tb[1] = 2.0;\n"
"\tc[1] = 3.0;\n"
"\td[1] = 4.0;\n"
"\te[1] = 1.0;\n"
"\n"
"\ta[2] = 1.0;\n"
"\tb[2] = 2.0;\n"
"\tc[2] = 3.0;\n"
"\td[2] = 4.0;\n"
"\te[2] = 1.0;\n"
"\n"
"\ta[3] = 1.0;\n"
"\tb[3] = 2.0;\n"
"\tc[3] = 3.0;\n"
"\td[3] = 4.0;\n"
"\te[3] = 1.0;\n"
"\n"
"\tqualifiers(a, b, c, d, e);\n"
"\n"
"\t// randomly test a value\n"
"\tif(a[0] == 1.0) q += 1.0;\n"
"\tif(b[1] == 1.0) q += 2.0;\n"
"\tif(c[2] == 7.0) q += 4.0;\n"
"\tif(d[3] == 4.0) q2 += 1.0;\n"
"\tif(e[0] == 1.0) q2 += 2.0;\n"
"\n"
"\tgl_FragColor = vec4(vec2(q / 7.0, q2 / 3.0), 1.0, 1.0);\n"
"}\n"
"\n"},
{SR_VS,8,
"#version 120\n"
"void main(void)\n"
"{\n"
"    gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,15,
"#version 120\n"
"void main(void)\n"
"{\n"
"    float x = 3.0;\n"
"    struct s { float f; vec3 v; };\n"
"    s a[1]=s[](s(x,vec3(x)));\n"
"    if (a[0].f==3.0)\n"
"      gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0); // green\n"
"    else\n"
"      gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); // red\n"
"}\n"
"\n"},
{SR_VS,7,
"void\n"
"main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"}\n"
"\n"
"\n"},
{SR_FS,15,
"struct S\n"
"{\n"
"\tvec4 v[1];\n"
"};\n"
"\n"
"uniform int zero;\n"
"\n"
"const S colors[1] = S[1](\n"
"\tS( vec4[1](vec4(0.0, 1.0, 0.0, 1.0)))\n"
");\n"
"\n"
"void\n"
"main()\n"
"{\n"
"\tS c = colors[zero];\n"
"\tgl_FragColor = c.v[zero];\n"
"}\n"
"\n"
"\n"},
{SR_VS,7,
"void\n"
"main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"}\n"
"\n"
"\n"},
{SR_FS,15,
"struct S\n"
"{\n"
"\tvec4 v;\n"
"};\n"
"\n"
"uniform int zero;\n"
"\n"
"const S colors[1] = S[1](\n"
"\tS( vec4(0.0, 1.0, 0.0, 1.0))\n"
");\n"
"\n"
"void\n"
"main()\n"
"{\n"
"\tS c = colors[zero];\n"
"\tgl_FragColor = c.v;\n"
"}\n"
"\n"
"\n"},
{SR_FS,6,
"void main()\n"
"{\n"
"\tmat2 a[2];\n"
"\ta[0][0][0] = 0;\n"
"\ta[int(a[0][0][0])][0][1] = 1;\n"
"\tif (a[0][0][1] == 1)\n"
"\t\tgl_FragColor = vec4(0, 1, 0, 1);\n"
"\telse \n"
"\t\tgl_FragColor = vec4(1, 0, 0, 1);\n"
"\t\n"
"\t\n"
"}\n"
"\n"},
{SR_VS,9,
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,15,
"struct Foo {\n"
"  ivec4 value;\n"
"};\n"
"\n"
"uniform int i;\n"
"uniform ivec4 expected_value;\n"
"\n"
"void main()\n"
"{\n"
"  ivec4 actual_value = Foo[2](Foo(ivec4(100, 200, 300, 400)),\n"
"                              Foo(ivec4(500, 600, 700, 800)))[i].value;\n"
"  if (actual_value == expected_value)\n"
"    gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);\n"
"  else\n"
"    gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_VS,13,
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,19,
"struct Foo {\n"
"  int value;\n"
"\n"
"  /* A second member of a differnt size ensures we calculate member offsets\n"
"   * correctly.\n"
"   */\n"
"  ivec2 value2;\n"
"};\n"
"\n"
"uniform int i;\n"
"uniform ivec2 expected_value;\n"
"\n"
"void main()\n"
"{\n"
"  ivec2 actual_value = Foo[4](Foo(100, ivec2(200, 300)),\n"
"                              Foo(400, ivec2(500, 600)),\n"
"                              Foo(700, ivec2(800, 900)),\n"
"                              Foo(1000, ivec2(1100, 1200)))[i].value2;\n"
"  if (actual_value == expected_value)\n"
"    gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);\n"
"  else\n"
"    gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_VS,9,
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,15,
"struct Foo {\n"
"  int value;\n"
"\n"
"  /* A second member of a differnt size helps ensures we calculate member\n"
"   * offsets correctly.\n"
"   */\n"
"  ivec2 value2;\n"
"};\n"
"\n"
"uniform int i;\n"
"uniform ivec2 expected_value;\n"
"\n"
"void main()\n"
"{\n"
"  ivec2 actual_value = Foo[2](Foo(100, ivec2(200, 300)),\n"
"                              Foo(400, ivec2(500, 600)))[i].value2;\n"
"  if (actual_value == expected_value)\n"
"    gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);\n"
"  else\n"
"    gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_VS,9,
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,15,
"struct Foo {\n"
"  int value;\n"
"};\n"
"\n"
"uniform int i;\n"
"uniform int expected_value;\n"
"\n"
"void main()\n"
"{\n"
"  int actual_value = Foo[2](Foo(100), Foo(200))[i].value;\n"
"  if (actual_value == expected_value)\n"
"    gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);\n"
"  else\n"
"    gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_PASSVS,7,
"\n"},
{SR_FS,9,
"#version 120\n"
"\n"
"struct S {\n"
"\tivec4 v;\n"
"};\n"
"\n"
"void multiply(inout S array[4])\n"
"{\n"
"\tfor (int i = 0; i < 4; ++i) {\n"
"\t\tarray[i].v *= i;\n"
"\t}\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"\tS array[4];\n"
"\n"
"\tfor (int i = 0; i < 4; ++i)\n"
"\t\tarray[i].v = ivec4(0, 1, 2, 3);\n"
"\n"
"\tmultiply(array);\n"
"\n"
"\tfor (int i = 0; i < 4; ++i) {\n"
"\t\tif (array[i].v.x != 0) {\n"
"\t\t\tgl_FragColor = vec4(1.0, 0.0, float(i) / 255.0, float(array[i].v.x) / 255.0);\n"
"\t\t\treturn;\n"
"\t\t}\n"
"\t\tif (array[i].v.y != 1 * i) {\n"
"\t\t\tgl_FragColor = vec4(1.0, 0.1, float(i) / 255.0, float(array[i].v.y) / 255.0);\n"
"\t\t\treturn;\n"
"\t\t}\n"
"\t\tif (array[i].v.z != 2 * i) {\n"
"\t\t\tgl_FragColor = vec4(1.0, 0.2, float(i) / 255.0, float(array[i].v.z) / 255.0);\n"
"\t\t\treturn;\n"
"\t\t}\n"
"\t\tif (array[i].v.w != 3 * i) {\n"
"\t\t\tgl_FragColor = vec4(1.0, 0.3, float(i) / 255.0, float(array[i].v.w) / 255.0);\n"
"\t\t\treturn;\n"
"\t\t}\n"
"\t}\n"
"\n"
"\tgl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_PASSVS,5,
"\n"},
{SR_FS,7,
"void main() {\n"
"   const bvec3 x = bvec3(false, false, true);\n"
"   const bvec3 y = bvec3(false, true, true);\n"
"   const bvec3 result = notEqual(x,y);\n"
"   gl_FragColor = vec4(result, 1.0);\n"
"}\n"
"\n"},
{SR_PASSVS,5,
"\n"},
{SR_FS,7,
"void main() {\n"
"   const vec3 small = vec3(1.0, 2.0, 3.0);\n"
"   const vec3 large = vec3(0.5, 2.0, 6.0);\n"
"   const bvec3 result = notEqual(large, small);\n"
"   gl_FragColor = vec4(result, 1.0);\n"
"}\n"
"\n"},
{SR_VS,7,
"void\n"
"main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"}\n"
"\n"
"\n"},
{SR_FS,15,
"\n"
"uniform int zero;\n"
"\n"
"const vec4 colors[] = vec4[](\n"
"\tvec4(0.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0)\n"
");\n"
"\n"
"void\n"
"main()\n"
"{\n"
"\tgl_FragColor = colors[zero];\n"
"}\n"
"\n"
"\n"},
{SR_VS,7,
"void\n"
"main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"}\n"
"\n"
"\n"},
{SR_FS,15,
"\n"
"uniform int zero;\n"
"\n"
"const vec4 colors[] = vec4[](\n"
"\tvec4(0.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 1.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0)\n"
");\n"
"\n"
"void\n"
"main()\n"
"{\n"
"\tgl_FragColor = colors[zero];\n"
"}\n"
"\n"
"\n"},
{SR_VS,7,
"void\n"
"main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"}\n"
"\n"
"\n"},
{SR_FS,15,
"\n"
"uniform int zero;\n"
"\n"
"const vec4 colors[] = vec4[](\n"
"\tvec4(0.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 1.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.1, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0)\n"
");\n"
"\n"
"void\n"
"main()\n"
"{\n"
"\tgl_FragColor = colors[zero];\n"
"}\n"
"\n"
"\n"},
{SR_VS,7,
"void\n"
"main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"}\n"
"\n"
"\n"},
{SR_FS,15,
"\n"
"uniform int zero;\n"
"\n"
"const vec4 colors[] = vec4[](\n"
"\tvec4(0.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 1.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.1, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.1)\n"
");\n"
"\n"
"void\n"
"main()\n"
"{\n"
"\tgl_FragColor = colors[zero];\n"
"}\n"
"\n"
"\n"},
{SR_VS,7,
"void\n"
"main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"}\n"
"\n"
"\n"},
{SR_FS,15,
"\n"
"uniform int zero;\n"
"\n"
"const vec4 colors[] = vec4[](\n"
"\tvec4(0.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 1.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.1, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.1),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.1)\n"
");\n"
"\n"
"void\n"
"main()\n"
"{\n"
"\tgl_FragColor = colors[zero];\n"
"}\n"
"\n"
"\n"},
{SR_VS,7,
"void\n"
"main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"}\n"
"\n"
"\n"},
{SR_FS,15,
"\n"
"uniform int zero;\n"
"\n"
"const vec4 colors[] = vec4[](\n"
"\tvec4(0.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 1.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.1, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.1),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 1.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.1, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.1)\n"
");\n"
"\n"
"void\n"
"main()\n"
"{\n"
"\tgl_FragColor = colors[zero];\n"
"}\n"
"\n"
"\n"},
{SR_VS,7,
"void\n"
"main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"}\n"
"\n"
"\n"},
{SR_FS,15,
"\n"
"uniform int zero;\n"
"\n"
"const vec4 colors[] = vec4[](\n"
"\tvec4(0.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 1.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.1, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.1),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 1.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.1, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.1),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 1.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.1, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.1),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 1.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.1, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.1)\n"
");\n"
"\n"
"void\n"
"main()\n"
"{\n"
"\tgl_FragColor = colors[zero];\n"
"}\n"
"\n"
"\n"},
{SR_VS,7,
"void\n"
"main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"}\n"
"\n"
"\n"},
{SR_FS,15,
"\n"
"uniform int zero;\n"
"\n"
"const vec4 colors[] = vec4[](\n"
"\tvec4(0.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 1.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.1, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.1),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 1.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.1, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.1),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 1.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.1, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.1),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 1.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.1, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.1),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 1.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.1, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.1),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 1.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.1, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.1),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 1.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.1, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.1),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 1.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.1, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.0, 1.0),\n"
"\tvec4(1.0, 1.0, 0.0, 1.0),\n"
"\tvec4(1.1, 1.0, 0.0, 1.0),\n"
"\tvec4(0.1, 1.0, 0.1, 1.1)\n"
");\n"
"\n"
"void\n"
"main()\n"
"{\n"
"\tgl_FragColor = colors[zero];\n"
"}\n"
"\n"
"\n"},
{SR_VS,5,
"#version 120\n"
"\n"
"void\n"
"main()\n"
"{\n"
"\tgl_Position = gl_Vertex;\n"
"\tgl_TexCoord[0] = vec4(0.0, 0.0, 0.0, 1.0);\n"
"\tgl_TexCoord[1] = gl_Vertex;\n"
"\tgl_TexCoord[2] = vec4(0.0, 0.0, 0.0, 0.0);\n"
"\tgl_TexCoord[3] = gl_Vertex * 2.0;\n"
"\tgl_TexCoord[4] = vec4(1.0, 1.0, 1.0, 1.0);\n"
"\tgl_TexCoord[5] = gl_Vertex * 4.0;\n"
"\tgl_TexCoord[6] = vec4(1.0, 1.0, 1.0, 0.0);\n"
"\tgl_TexCoord[7] = gl_Vertex * 8.0;\n"
"}\n"
"\n"
"\n"},
{SR_FS,23,
"#version 120\n"
"\n"
"void\n"
"main()\n"
"{\n"
"\tif (gl_TexCoord[0] == vec4(0.0, 0.0, 0.0, 1.0) &&\n"
"\t    gl_TexCoord[2] == vec4(0.0, 0.0, 0.0, 0.0) &&\n"
"\t    gl_TexCoord[3] == gl_TexCoord[1] * 2.0 &&\n"
"\t    gl_TexCoord[4] == vec4(1.0, 1.0, 1.0, 1.0) &&\n"
"\t    gl_TexCoord[5] == gl_TexCoord[1] * 4.0 &&\n"
"\t    gl_TexCoord[6] == vec4(1.0, 1.0, 1.0, 0.0) &&\n"
"\t    gl_TexCoord[7] == gl_TexCoord[1] * 8.0)\n"
"\t\tgl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);\n"
"\telse\n"
"\t\tgl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n"
"}\n"
"\n"
"\n"},
{SR_PASSVS,5,
"\n"},
{SR_FS,7,
"uniform int u = 1;\n"
"\n"
"void func(inout int i, inout int w[4], inout int x)\n"
"{\n"
"  i = 0;\n"
"  for (int j = 0; j < 4; ++j)\n"
"    w[j] += j;\n"
"  x = 1;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"  int v[4] = int[](0, 0, 0, 0);\n"
"  int w[4] = int[](0, 1, 0, 0);\n"
"  int v_expected[4] = int[](0, 1, 0, 0);\n"
"  int w_expected[4] = int[](0, 2, 2, 3);\n"
"  int i = u;\n"
"\n"
"  func(i, w, v[w[i]]);\n"
"\n"
"  if (i != 0) {\n"
"    gl_FragColor = vec4(1.0, 0.0, i * 0.1, 0.0);\n"
"    return;\n"
"  }\n"
"\n"
"  for (i = 0; i < 4; ++i) {\n"
"    if (v[i] != v_expected[i]) {\n"
"      gl_FragColor = vec4(1.0, 0.1 * (1 + i), v[i] * 0.1, 0.0);\n"
"      return;\n"
"    }\n"
"    if (w[i] != w_expected[i]) {\n"
"      gl_FragColor = vec4(1.0, 0.1 * (5 + i), w[i] * 0.1, 0.0);\n"
"      return;\n"
"    }\n"
"  }\n"
"\n"
"  gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_VS,5,
"\n"
"uniform int u = 1;\n"
"varying vec4 color;\n"
"\n"
"void func(inout int i, inout float f)\n"
"{\n"
"  i = 0;\n"
"  f = 1.;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"  float fa[] = float[](0., 0., 0., 0.);\n"
"  int i = u;\n"
"\n"
"  func(i, fa[i]);\n"
"  color = vec4(fa[0], fa[1], fa[2], fa[3]);\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,26,
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"  gl_FragColor = color;\n"
"}\n"
"\n"},
{SR_VS,5,
"uniform int u = 1;\n"
"varying vec4 color;\n"
"\n"
"void func(inout int i, inout float f)\n"
"{\n"
"  i = 0;\n"
"  f = 1.;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"  mat2 m = mat2(0.);\n"
"  ivec4[] n = ivec4[](ivec4(0, 1, 1, 1), ivec4(1, 0, 0, 0));\n"
"  int i = u;\n"
"\n"
"  func(i, m[n[i].x][1]);\n"
"  color = vec4(m[1].x, m[1].y, m[0].x, m[0].y);\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,26,
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"  gl_FragColor = color;\n"
"}\n"
"\n"},
{SR_VS,5,
"uniform int u = 1;\n"
"varying vec4 color;\n"
"\n"
"void func(inout int i, inout float f)\n"
"{\n"
"  i = 0;\n"
"  f = 1.;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"  mat2 m = mat2(0.);\n"
"  int i = u;\n"
"\n"
"  func(i, m[i][1]);\n"
"  color = vec4(m[1].x, m[1].y, m[0].x, m[0].y);\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,25,
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"  gl_FragColor = color;\n"
"}\n"
"\n"},
{SR_VS,5,
"uniform int u = 1;\n"
"varying vec4 color;\n"
"\n"
"void func(inout int i, inout float f)\n"
"{\n"
"  i = 0;\n"
"  f = 1.;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"  mat2 m = mat2(0.);\n"
"  int i = u;\n"
"\n"
"  func(i, m[0][i]);\n"
"  color = vec4(m[0].x, m[0].y, m[1].x, m[1].y);\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,25,
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"  gl_FragColor = color;\n"
"}\n"
"\n"},
{SR_VS,5,
"\n"
"uniform int u = 1;\n"
"varying vec4 color;\n"
"\n"
"void func(inout int i, inout float f)\n"
"{\n"
"  i = 0;\n"
"  f = 1.;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"  vec4 v[] = vec4[](vec4(0.), vec4(0.));\n"
"  int w[] = int[](1, 0, 0, 0);\n"
"  int i = u;\n"
"\n"
"  func(i, v[1][w[i]]);\n"
"  color = v[1];\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,27,
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"  gl_FragColor = color;\n"
"}\n"
"\n"},
{SR_VS,5,
"\n"
"uniform int u = 1;\n"
"varying vec4 color;\n"
"\n"
"void func(inout int i, inout float f)\n"
"{\n"
"  i = 0;\n"
"  f = 1.;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"  vec4 v[] = vec4[](vec4(0.), vec4(0.));\n"
"  int i = u;\n"
"\n"
"  func(i, v[1][i]);\n"
"  color = v[1];\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,26,
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"  gl_FragColor = color;\n"
"}\n"
"\n"},
{SR_VS,5,
"\n"
"uniform int u = 1;\n"
"varying vec4 color;\n"
"\n"
"void func(inout int i, inout float f)\n"
"{\n"
"  i = 0;\n"
"  f = 1.;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"  vec4 v[] = vec4[](vec4(0.), vec4(0.));\n"
"  int i = u;\n"
"\n"
"  func(i, v[i].y);\n"
"  color = v[1];\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,26,
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"  gl_FragColor = color;\n"
"}\n"
"\n"},
{SR_VS,5,
"\n"
"uniform int u = 1;\n"
"varying vec4 color;\n"
"\n"
"void func(inout int i, inout float f)\n"
"{\n"
"  i = 0;\n"
"  f = 1.;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"  vec4 v = vec4(0.);\n"
"  int i = u;\n"
"\n"
"  func(i, v[i]);\n"
"  color = v;\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,26,
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"  gl_FragColor = color;\n"
"}\n"
"\n"},
{SR_VS,5,
"\n"
"uniform int u = 1;\n"
"varying vec4 color;\n"
"\n"
"void func(inout float f)\n"
"{\n"
"  f = 1.;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"  vec4 v = vec4(0.);\n"
"\n"
"  func(v[u]);\n"
"  color = v;\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,24,
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"  gl_FragColor = color;\n"
"}\n"
"\n"},
{SR_VS,5,
"\n"
"uniform int u = 1;\n"
"varying vec4 color;\n"
"\n"
"void func(out float f)\n"
"{\n"
"  f = 1.;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"  vec4 v = vec4(0.);\n"
"\n"
"  func(v[u]);\n"
"  color = v;\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,24,
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"  gl_FragColor = color;\n"
"}\n"
"\n"},
{SR_VS,19,
"#version 120\n"
"float f(out int x, out int y, out float z)\n"
"{\n"
"  x = 4;\n"
"  y = 5;\n"
"  z = 0.75;\n"
"  return 0.5;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"  float value1;\n"
"  float value2;\n"
"  float value3;\n"
"  float value4 = f(value1, value2, value3);\n"
"  gl_FrontColor = vec4(1.0/value1, 1.0/value2, value3, value4);\n"
"}\n"
"\n"},
{SR_FS,39,
"void main()\n"
"{\n"
"  gl_FragColor = gl_Color;\n"
"}\n"
"\n"},
{SR_VS,18,
"#version 120\n"
"float f(out int x)\n"
"{\n"
"  x = 4;\n"
"  return 0.5;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"  float value1;\n"
"  float value2 = f(value1);\n"
"  gl_FrontColor = vec4(1.0/value1, value2, 0.0, 0.0);\n"
"}\n"
"\n"},
{SR_FS,34,
"void main()\n"
"{\n"
"  gl_FragColor = gl_Color;\n"
"}\n"
"\n"},
{SR_VS,20,
"#version 120\n"
"\n"
"uniform int u = 1;\n"
"\n"
"void f(inout int i, out int x)\n"
"{\n"
"  x = 4;\n"
"  i = 0;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"  vec4 value;\n"
"\n"
"  int i = u;\n"
"  f(i, value[i]);\n"
"  gl_FrontColor = vec4(1.0/value[1]);\n"
"}\n"
"\n"},
{SR_FS,41,
"void main()\n"
"{\n"
"  gl_FragColor = gl_Color;\n"
"}\n"
"\n"},
{SR_VS,18,
"#version 120\n"
"void f(out int x)\n"
"{\n"
"  x = 4;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"  float value;\n"
"  f(value);\n"
"  gl_FrontColor = vec4(1.0/value);\n"
"}\n"
"\n"},
{SR_FS,33,
"void main()\n"
"{\n"
"  gl_FragColor = gl_Color;\n"
"}\n"
"\n"},
{SR_VS,5,
"#version 120\n"
"\n"
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,13,
"#version 120\n"
"\n"
"struct S {\n"
"  int i;\n"
"  float f;\n"
"  vec4 v[2];\n"
"};\n"
"\n"
"uniform S s[2] = S[2](S(9, 33.3, vec4[2](vec4(1, 2, 3, 4),\n"
"\t\t\t\t\t vec4(9, 8, 7, 6))),\n"
"\t\t      S(7, 22.2, vec4[2](vec4(5, 6, 7, 8),\n"
"\t\t\t\t\t vec4(3, 2, 1, 0))));\n"
"\n"
"void main()\n"
"{\n"
"  vec4 color;\n"
"\n"
"  if (s[0].i == 9\n"
"      && s[0].f == 33.3\n"
"      && s[0].v[0] == vec4(1, 2, 3, 4)\n"
"      && s[0].v[1] == vec4(9, 8, 7, 6)\n"
"      && s[1].i == 7\n"
"      && s[1].f == 22.2\n"
"      && s[1].v[0] == vec4(5, 6, 7, 8)\n"
"      && s[1].v[1] == vec4(3, 2, 1, 0)) {\n"
"    color = vec4(0, 1, 0, 1);\n"
"  } else {\n"
"    color = vec4(1, 0, 0, 1);\n"
"  }\n"
"\n"
"  gl_FragColor = color;\n"
"}\n"
"\n"},
{SR_VS,5,
"#version 120\n"
"\n"
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,13,
"#version 120\n"
"\n"
"struct S {\n"
"  int i;\n"
"  float f;\n"
"  vec4 v[2];\n"
"};\n"
"\n"
"uniform S s = S(9, 33.3, vec4[2](vec4(1, 2, 3, 4),\n"
"\t\t\t\t vec4(9, 8, 7, 6)));\n"
"\n"
"void main()\n"
"{\n"
"  vec4 color;\n"
"\n"
"  if (s.i == 9\n"
"      && s.f == 33.3\n"
"      && s.v[0] == vec4(1, 2, 3, 4)\n"
"      && s.v[1] == vec4(9, 8, 7, 6)) {\n"
"    color = vec4(0, 1, 0, 1);\n"
"  } else {\n"
"    color = vec4(1, 0, 0, 1);\n"
"  }\n"
"\n"
"\n"
"  gl_FragColor = color;\n"
"}\n"
"\n"},
{SR_VS,5,
"#version 120\n"
"\n"
"struct S {\n"
"  int i;\n"
"  float f;\n"
"  vec4 v[2];\n"
"};\n"
"\n"
"uniform S s[2] = S[2](S(9, 33.3, vec4[2](vec4(1, 2, 3, 4),\n"
"\t\t\t\t\t vec4(9, 8, 7, 6))),\n"
"\t\t      S(7, 22.2, vec4[2](vec4(5, 6, 7, 8),\n"
"\t\t\t\t\t vec4(3, 2, 1, 0))));\n"
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"  if (s[0].i == 9\n"
"      && s[0].f == 33.3\n"
"      && s[0].v[0] == vec4(1, 2, 3, 4)\n"
"      && s[0].v[1] == vec4(9, 8, 7, 6)\n"
"      && s[1].i == 7\n"
"      && s[1].f == 22.2\n"
"      && s[1].v[0] == vec4(5, 6, 7, 8)\n"
"      && s[1].v[1] == vec4(3, 2, 1, 0)) {\n"
"    color = vec4(0, 1, 0, 1);\n"
"  } else {\n"
"    color = vec4(1, 0, 0, 1);\n"
"  }\n"
"\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,38,
"#version 120\n"
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"  gl_FragColor = color;\n"
"}\n"
"\n"},
{SR_VS,5,
"#version 120\n"
"\n"
"struct S {\n"
"  int i;\n"
"  float f;\n"
"  vec4 v[2];\n"
"};\n"
"\n"
"uniform S s = S(9, 33.3, vec4[2](vec4(1, 2, 3, 4),\n"
"\t\t\t\t vec4(9, 8, 7, 6)));\n"
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"  if (s.i == 9\n"
"      && s.f == 33.3\n"
"      && s.v[0] == vec4(1, 2, 3, 4)\n"
"      && s.v[1] == vec4(9, 8, 7, 6)) {\n"
"    color = vec4(0, 1, 0, 1);\n"
"  } else {\n"
"    color = vec4(1, 0, 0, 1);\n"
"  }\n"
"\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,32,
"#version 120\n"
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"  gl_FragColor = color;\n"
"}\n"
"\n"},
{SR_VS,5,
"#version 120\n"
"varying vec4 color;\n"
"\n"
"uniform bool[2] ub = bool[2](bool(0), bool(1));\n"
"\n"
"void main()\n"
"{\n"
"  if (ub == bool[2](bool(1), bool(1))) {\n"
"    color = vec4(1, 0, 0, 1);\n"
"  } else {\n"
"    color = vec4(0, 1, 0, 1);\n"
"  }\n"
"\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,22,
"#version 120\n"
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"  gl_FragColor = color;\n"
"}\n"
"\n"},
{SR_VS,11,
"#version 120\n"
"void main(void)\n"
"{\n"
"    float x = 3.0;\n"
"    struct s { float f; vec3 v; };\n"
"    s a[1]=s[](s(x,vec3(x)));\n"
"    if (a[0].f==3.0)\n"
"      gl_FrontColor = vec4(0.0, 1.0, 0.0, 1.0); // green\n"
"    else\n"
"      gl_FrontColor = vec4(1.0, 0.0, 0.0, 1.0); // red\n"
"    gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,25,
"void main(void)\n"
"{\n"
"    gl_FragColor = gl_Color;\n"
"}\n"
"\n"},
{SR_VS,9,
"struct Foo {\n"
"  int value;\n"
"};\n"
"\n"
"uniform int i;\n"
"uniform int expected_value;\n"
"\n"
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"  int actual_value = Foo[2](Foo(100), Foo(200))[i].value;\n"
"  if (actual_value == expected_value)\n"
"    gl_FrontColor = vec4(0.0, 1.0, 0.0, 1.0);\n"
"  else\n"
"    gl_FrontColor = vec4(1.0, 0.0, 0.0, 1.0);\n"
"}\n"
"\n"},
{SR_VS,13,
"#version 120\n"
"varying vec4 color;\n"
"\n"
"void array_mod(inout int b[2])\n"
"{\n"
"  b[0] = int(2);\n"
"  b[1] = b[1] * int(1);\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"  int a[2];\n"
"\n"
"  a[0] = int(1);\n"
"  a[1] = int(2);\n"
"  array_mod(a);\n"
"\n"
"  if (a[0] == int(2) && a[1] == int(2)) {\n"
"    color = vec4(0, 1, 0, 1);\n"
"  } else {\n"
"    color = vec4(1, 0, 0, 1);\n"
"  }\n"
"\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_FS,40,
"#version 120\n"
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"  gl_FragColor = color;\n"
"}\n"
"\n"},
{SR_VS,5,
"uniform vec4 red = vec4(1.0, 0.0, 0.0, 1.0);\n"
"uniform vec4 color_delta = vec4(-1.0, 1.0, 0.0, 0.0);\n"
"uniform float a = -1.0;\n"
"uniform float b = 1.0;\n"
"uniform float c = 0.5;\n"
"\n"
"attribute vec4 piglit_vertex;\n"
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"    gl_Position = piglit_vertex;\n"
"\n"
"    color = red;\n"
"\n"
"    /* This attempts to reproduce a bug in the i965 vec4 backend.  Three-\n"
"     * source instructions, like LRP, cannot write to the null register.\n"
"     * However, when the result is only used in a condition, the write can be\n"
"     * (erroneously) eliminated.\n"
"     */\n"
"    if (mix(a, b, c) == 0)\n"
"        color += color_delta;\n"
"}\n"
"\n"},
{SR_FS,30,
"varying vec4 color;\n"
"\n"
"void main()\n"
"{\n"
"    gl_FragColor = color;\n"
"}\n"
"\n"},
{SR_VS,5,
"#version 120\n"
"centroid varying float var;\n"
"\n"
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_VS,14,
"#version 120\n"
"varying float var;\n"
"\n"
"void myfunc()\n"
"{\n"
"}\n"
"\n"},
{SR_VS,7,
"uniform int index;\n"
"void main() {\n"
"\tconst float zs[4] = float[4](0, 0, 0, 0);\n"
"\tgl_Position = vec4(zs[index]);\n"
"}\n"
"\n"},
{SR_FS,14,
"uniform int index;\n"
"void main()\n"
"{\n"
"\tconst vec4 colours[2] = vec4[2](vec4(1, 0, 0, 0), vec4(0, 1, 0, 0));\n"
"\tgl_FragColor = vec4(colours[index].x);\n"
"}\n"
"\n"},
{SR_VS,8,
"#version 120\n"
"\n"
"varying vec4 color[3];\n"
"\n"
"void f()\n"
"{\n"
"  color[1] = vec4(1, 0, 0, 1);\n"
"}\n"
"\n"},
{SR_VS,18,
"#version 120\n"
"\n"
"varying vec4 color[];\n"
"\n"
"void f();\n"
"\n"
"void main()\n"
"{\n"
"  f();\n"
"  color[3] = vec4(1, 0, 0, 1);\n"
"\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_VS,8,
"#version 120\n"
"\n"
"varying vec4 color[];\n"
"\n"
"void f()\n"
"{\n"
"  color[3] = vec4(1, 0, 0, 1);\n"
"}\n"
"\n"},
{SR_VS,18,
"#version 120\n"
"\n"
"varying vec4 color[3];\n"
"\n"
"void f();\n"
"\n"
"void main()\n"
"{\n"
"  f();\n"
"  color[2] = vec4(1, 0, 0, 1);\n"
"\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_VS,8,
"#version 120\n"
"\n"
"varying vec4 color[4];\n"
"\n"
"void f()\n"
"{\n"
"  color[1] = vec4(1, 0, 0, 1);\n"
"}\n"
"\n"},
{SR_VS,18,
"#version 120\n"
"\n"
"varying vec4 color[];\n"
"\n"
"void f();\n"
"\n"
"void main()\n"
"{\n"
"  f();\n"
"  color[3] = vec4(1, 0, 0, 1);\n"
"\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_VS,8,
"#version 120\n"
"\n"
"varying vec4 color[];\n"
"\n"
"void f()\n"
"{\n"
"  color[3] = vec4(1, 0, 0, 1);\n"
"}\n"
"\n"},
{SR_VS,18,
"#version 120\n"
"\n"
"varying vec4 color[4];\n"
"\n"
"void f();\n"
"\n"
"void main()\n"
"{\n"
"  f();\n"
"  color[1] = vec4(1, 0, 0, 1);\n"
"\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_VS,5,
"#version 120\n"
"invariant varying float var;\n"
"\n"
"void main()\n"
"{\n"
"  gl_Position = gl_Vertex;\n"
"}\n"
"\n"},
{SR_VS,14,
"#version 120\n"
"varying float var;\n"
"\n"
"void myfunc()\n"
"{\n"
"}\n"
"\n"},
{SR_VS,8,
"#version 120\n"
"invariant gl_Position;\n"
"void main()\n"
"{\n"
"  gl_Position = ftransform();\n"
"}\n"
"\n"
"\n"},
{SR_FS,17,
"#version 120\n"
"void main()\n"
"{\n"
"  gl_FragColor = vec4(1.0);\n"
"}\n"
"\n"
"\n"},
{SR_VS,15,
"// Shader A\n"
"vec4 vals[];\n"
"\n"
"void set_position()\n"
"{\n"
"\tgl_Position = vals[1];\n"
"}\n"
"\n"},
{SR_VS,24,
"// Shader B\n"
"vec4 vals[2];\n"
"\n"
"void set_position();\n"
"\n"
"void main()\n"
"{\n"
"\tvals[1] = gl_Vertex;\n"
"\tset_position();\n"
"}\n"
"\n"
"\n"},
{SR_FS,37,
"void main()\n"
"{\n"
"\tgl_FragColor = vec4(0.0, 1.0, 0.0, 0.0);\n"
"}\n"
"\n"},
{SR_VS,15,
"// Shader B\n"
"vec4 vals[2];\n"
"\n"
"void set_position();\n"
"\n"
"void main()\n"
"{\n"
"\tvals[1] = gl_Vertex;\n"
"\tset_position();\n"
"}\n"
"\n"},
{SR_VS,27,
"// Shader A\n"
"vec4 vals[];\n"
"\n"
"void set_position()\n"
"{\n"
"\tgl_Position = vals[1];\n"
"}\n"
"\n"
"\n"},
{SR_FS,37,
"void main()\n"
"{\n"
"\tgl_FragColor = vec4(0.0, 1.0, 0.0, 0.0);\n"
"}\n"
"\n"},
};
static const SrCommand sr_commands[]={
{18,"parameter env_vp 0 (-1.0, 0.0, 0.0, -0.5)",SR_P_ENV_VP,0,NULL,{0,-1.0,0.0,0.0,-0.5}},
{19,"parameter env_vp 1 (0.2, 0.0, 0.0, 0.0)",SR_P_ENV_VP,0,NULL,{1,0.2,0.0,0.0,0.0}},
{20,"draw rect -1 -1 1 2",SR_QUAD,0,NULL,{-1,-1,1,2}},
{21,"relative probe rgba (0.25, 0.5) (0.2, 0.2, 0.2, 1.0);",SR_REL4,0,NULL,{0.25,0.5,0.2,0.2,0.2,1.0}},
{23,"parameter env_vp 0 (-1.0, 0.0, 0.0, -0.5)",SR_P_ENV_VP,0,NULL,{0,-1.0,0.0,0.0,-0.5}},
{24,"parameter env_vp 1 (0.2, 0.0, 0.0, 0.5)",SR_P_ENV_VP,0,NULL,{1,0.2,0.0,0.0,0.5}},
{25,"draw rect 0 -1 1 2",SR_QUAD,0,NULL,{0,-1,1,2}},
{26,"relative probe rgba (0.75, 0.5) (0.7, 0.7, 0.7, 1.0);",SR_REL4,0,NULL,{0.75,0.5,0.7,0.7,0.7,1.0}},
{24,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{25,"clear color 1.0 0.0 0.0 0.0",SR_CLEARCOLOR,0,NULL,{1.0,0.0,0.0,0.0}},
{26,"clear",SR_CLEAR,0,NULL,{0}},
{27,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{28,"probe all rgba 0.2 0.6 0.8 1.0",SR_ALL4,0,NULL,{0.2,0.6,0.8,1.0}},
{32,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{33,"clear color 1.0 1.0 1.0 1.0",SR_CLEARCOLOR,0,NULL,{1.0,1.0,1.0,1.0}},
{34,"clear",SR_CLEAR,0,NULL,{0}},
{35,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{36,"probe all rgba 0.0 0.4 0.0 0.16",SR_ALL4,0,NULL,{0.0,0.4,0.0,0.16}},
{24,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{25,"clear color 0.0 0.0 0.0 0.0",SR_CLEARCOLOR,0,NULL,{0.0,0.0,0.0,0.0}},
{26,"clear",SR_CLEAR,0,NULL,{0}},
{27,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{28,"probe all rgba 1.0 1.0 1.0 1.0",SR_ALL4,0,NULL,{1.0,1.0,1.0,1.0}},
{17,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{18,"clear color 1.0 0.0 1.0 0.0",SR_CLEARCOLOR,0,NULL,{1.0,0.0,1.0,0.0}},
{19,"clear",SR_CLEAR,0,NULL,{0}},
{20,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{21,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{20,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{21,"parameter local_fp 0 (0.1, 0.2, 0.3, 0.4)",SR_P_LOCAL_FP,0,NULL,{0,0.1,0.2,0.3,0.4}},
{22,"parameter local_fp 1 (0.5, 0.5, 0.5, 0.5)",SR_P_LOCAL_FP,0,NULL,{1,0.5,0.5,0.5,0.5}},
{23,"clear color 0.0 1.0 1.0 1.0",SR_CLEARCOLOR,0,NULL,{0.0,1.0,1.0,1.0}},
{24,"clear",SR_CLEAR,0,NULL,{0}},
{25,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{26,"probe all rgba 1.0 0.1 0.2 0.5",SR_ALL4,0,NULL,{1.0,0.1,0.2,0.5}},
{20,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{21,"parameter local_fp 0 (0.1, 0.2, 0.3, 0.4)",SR_P_LOCAL_FP,0,NULL,{0,0.1,0.2,0.3,0.4}},
{22,"parameter local_fp 1 (0.5, 0.5, 0.5, 0.5)",SR_P_LOCAL_FP,0,NULL,{1,0.5,0.5,0.5,0.5}},
{23,"clear color 0.0 1.0 1.0 1.0",SR_CLEARCOLOR,0,NULL,{0.0,1.0,1.0,1.0}},
{24,"clear",SR_CLEAR,0,NULL,{0}},
{25,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{26,"probe all rgba 1.0 0.1 0.3 0.5",SR_ALL4,0,NULL,{1.0,0.1,0.3,0.5}},
{19,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{20,"parameter local_fp 0 (2.0, 0.0, 0.0, 0.0)",SR_P_LOCAL_FP,0,NULL,{0,2.0,0.0,0.0,0.0}},
{21,"clear color 0.0 0.0 0.0 0.0",SR_CLEARCOLOR,0,NULL,{0.0,0.0,0.0,0.0}},
{22,"clear",SR_CLEAR,0,NULL,{0}},
{23,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{24,"probe all rgba 0.75 0.75 0.75 0.75",SR_ALL4,0,NULL,{0.75,0.75,0.75,0.75}},
{23,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{24,"clear color 0.0 1.0 0.0 1.0",SR_CLEARCOLOR,0,NULL,{0.0,1.0,0.0,1.0}},
{25,"clear",SR_CLEAR,0,NULL,{0}},
{26,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{27,"probe all rgba 1.0 0.0 1.0 0.0",SR_ALL4,0,NULL,{1.0,0.0,1.0,0.0}},
{22,"clear color 0 0 1 0",SR_CLEARCOLOR,0,NULL,{0,0,1,0}},
{24,"parameter env_vp 0 (-1.0, -1.0, -1.0, -1.0)",SR_P_ENV_VP,0,NULL,{0,-1.0,-1.0,-1.0,-1.0}},
{25,"parameter env_vp 1 (-1.0, -1.0, -1.0, -1.0)",SR_P_ENV_VP,0,NULL,{1,-1.0,-1.0,-1.0,-1.0}},
{26,"clear",SR_CLEAR,0,NULL,{0}},
{27,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{28,"probe all rgba 0 0 1 0",SR_ALL4,0,NULL,{0,0,1,0}},
{31,"parameter env_vp 0 ( 1.0,  1.0, -1.0, -1.0)",SR_P_ENV_VP,0,NULL,{0,1.0,1.0,-1.0,-1.0}},
{32,"parameter env_vp 1 (-1.0, -1.0,  1.0,  1.0)",SR_P_ENV_VP,0,NULL,{1,-1.0,-1.0,1.0,1.0}},
{33,"clear",SR_CLEAR,0,NULL,{0}},
{34,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{35,"probe all rgba 0 1 0 0",SR_ALL4,0,NULL,{0,1,0,0}},
{38,"parameter env_vp 0 ( 1.0,  1.0, -1.0, -1.0)",SR_P_ENV_VP,0,NULL,{0,1.0,1.0,-1.0,-1.0}},
{39,"parameter env_vp 1 (-1.0, -1.0, -1.0,  1.0)",SR_P_ENV_VP,0,NULL,{1,-1.0,-1.0,-1.0,1.0}},
{40,"clear",SR_CLEAR,0,NULL,{0}},
{41,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{42,"probe all rgba 0 0 1 0",SR_ALL4,0,NULL,{0,0,1,0}},
{45,"parameter env_vp 0 ( 1.0,  1.0, -1.0, -1.0)",SR_P_ENV_VP,0,NULL,{0,1.0,1.0,-1.0,-1.0}},
{46,"parameter env_vp 1 (-1.0, -1.0,  1.0, -1.0)",SR_P_ENV_VP,0,NULL,{1,-1.0,-1.0,1.0,-1.0}},
{47,"clear",SR_CLEAR,0,NULL,{0}},
{48,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{49,"probe all rgba 0 0 1 0",SR_ALL4,0,NULL,{0,0,1,0}},
{52,"parameter env_vp 0 ( 1.0, -1.0, -1.0, -1.0)",SR_P_ENV_VP,0,NULL,{0,1.0,-1.0,-1.0,-1.0}},
{53,"parameter env_vp 1 (-1.0, -1.0,  1.0,  1.0)",SR_P_ENV_VP,0,NULL,{1,-1.0,-1.0,1.0,1.0}},
{54,"clear",SR_CLEAR,0,NULL,{0}},
{55,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{56,"probe all rgba 0 0 1 0",SR_ALL4,0,NULL,{0,0,1,0}},
{59,"parameter env_vp 0 (-1.0,  1.0, -1.0, -1.0)",SR_P_ENV_VP,0,NULL,{0,-1.0,1.0,-1.0,-1.0}},
{60,"parameter env_vp 1 (-1.0, -1.0,  1.0,  1.0)",SR_P_ENV_VP,0,NULL,{1,-1.0,-1.0,1.0,1.0}},
{61,"clear",SR_CLEAR,0,NULL,{0}},
{62,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{63,"probe all rgba 0 0 1 0",SR_ALL4,0,NULL,{0,0,1,0}},
{20,"parameter env_vp 0 (0.5, 0.0, 1.0, 0.0)",SR_P_ENV_VP,0,NULL,{0,0.5,0.0,1.0,0.0}},
{21,"parameter env_vp 1 (1.5, 4.5, 2.0, 1.0)",SR_P_ENV_VP,0,NULL,{1,1.5,4.5,2.0,1.0}},
{22,"parameter env_vp 2 (-0.5, 4.5, 3.5, 1.0)",SR_P_ENV_VP,0,NULL,{2,-0.5,4.5,3.5,1.0}},
{23,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{24,"probe all rgba 0.25 0.5 0.5 0.5",SR_ALL4,0,NULL,{0.25,0.5,0.5,0.5}},
{24,"texture rgbw 0 (8, 8)",SR_RGB2,0,NULL,{0,8,8}},
{25,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{30,"relative probe rgba (0.0, 0.0) (1.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.0,0.0,1.0,0.0,0.0,1.0}},
{31,"relative probe rgba (0.4, 0.4) (1.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.4,0.4,1.0,0.0,0.0,1.0}},
{33,"relative probe rgba (1.0, 0.0) (0.0, 1.0, 0.0, 1.0)",SR_REL4,0,NULL,{1.0,0.0,0.0,1.0,0.0,1.0}},
{34,"relative probe rgba (0.6, 0.4) (0.0, 1.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.6,0.4,0.0,1.0,0.0,1.0}},
{36,"relative probe rgba (0.0, 1.0) (0.0, 0.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.0,1.0,0.0,0.0,1.0,1.0}},
{37,"relative probe rgba (0.4, 0.6) (0.0, 0.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.4,0.6,0.0,0.0,1.0,1.0}},
{39,"relative probe rgba (1.0, 1.0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{1.0,1.0,1.0,1.0,1.0,1.0}},
{40,"relative probe rgba (0.6, 0.6) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.6,0.6,1.0,1.0,1.0,1.0}},
{23,"clear color 0.5 0.5 0.5 0.5",SR_CLEARCOLOR,0,NULL,{0.5,0.5,0.5,0.5}},
{24,"clear",SR_CLEAR,0,NULL,{0}},
{25,"ortho",SR_ORTHOWINDOW,0,NULL,{0}},
{28,"texture miptree 0",SR_MIP,0,NULL,{0}},
{30,"parameter env_vp 0 (-1.0,  -1.0, 0.0625, 0.0)",SR_P_ENV_VP,0,NULL,{0,-1.0,-1.0,0.0625,0.0}},
{31,"draw rect 01 1 16 16",SR_QUAD,0,NULL,{01,1,16,16}},
{32,"parameter env_vp 0 (-21.0, -1.0, 0.125,  0.0)",SR_P_ENV_VP,0,NULL,{0,-21.0,-1.0,0.125,0.0}},
{33,"draw rect 21 1 8 8",SR_QUAD,0,NULL,{21,1,8,8}},
{34,"parameter env_vp 0 (-41.0, -1.0, 0.25,  0.0)",SR_P_ENV_VP,0,NULL,{0,-41.0,-1.0,0.25,0.0}},
{35,"draw rect 41 1 4 4",SR_QUAD,0,NULL,{41,1,4,4}},
{36,"parameter env_vp 0 (-61.0, -1.0, 0.5,  0.0)",SR_P_ENV_VP,0,NULL,{0,-61.0,-1.0,0.5,0.0}},
{37,"draw rect 61 1 2 2",SR_QUAD,0,NULL,{61,1,2,2}},
{38,"parameter env_vp 0 (-81.0, -1.0, 1.0,  0.0)",SR_P_ENV_VP,0,NULL,{0,-81.0,-1.0,1.0,0.0}},
{39,"draw rect 81 1 1 1",SR_QUAD,0,NULL,{81,1,1,1}},
{41,"probe rgba  1  1 1.0 0.0 0.0 1.0",SR_PIX4,0,NULL,{1,1,1.0,0.0,0.0,1.0}},
{42,"probe rgba 21  1 0.0 1.0 0.0 1.0",SR_PIX4,0,NULL,{21,1,0.0,1.0,0.0,1.0}},
{43,"probe rgba 41  1 0.0 0.0 1.0 1.0",SR_PIX4,0,NULL,{41,1,0.0,0.0,1.0,1.0}},
{44,"probe rgba 61  1 1.0 1.0 1.0 1.0",SR_PIX4,0,NULL,{61,1,1.0,1.0,1.0,1.0}},
{45,"probe rgba 81  1 1.0 1.0 1.0 1.0",SR_PIX4,0,NULL,{81,1,1.0,1.0,1.0,1.0}},
{23,"texture rgbw 0 (8, 8)",SR_RGB2,0,NULL,{0,8,8}},
{24,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{29,"relative probe rgba (0.0, 0.0) (1.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.0,0.0,1.0,0.0,0.0,1.0}},
{30,"relative probe rgba (0.4, 0.4) (1.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.4,0.4,1.0,0.0,0.0,1.0}},
{32,"relative probe rgba (1.0, 0.0) (0.0, 1.0, 0.0, 1.0)",SR_REL4,0,NULL,{1.0,0.0,0.0,1.0,0.0,1.0}},
{33,"relative probe rgba (0.6, 0.4) (0.0, 1.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.6,0.4,0.0,1.0,0.0,1.0}},
{35,"relative probe rgba (0.0, 1.0) (0.0, 0.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.0,1.0,0.0,0.0,1.0,1.0}},
{36,"relative probe rgba (0.4, 0.6) (0.0, 0.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.4,0.6,0.0,0.0,1.0,1.0}},
{38,"relative probe rgba (1.0, 1.0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{1.0,1.0,1.0,1.0,1.0,1.0}},
{39,"relative probe rgba (0.6, 0.6) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.6,0.6,1.0,1.0,1.0,1.0}},
{20,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{21,"parameter local_vp 0 (0.0, 1.0, 0.0, 1.0)",SR_P_LOCAL_VP,0,NULL,{0,0.0,1.0,0.0,1.0}},
{22,"parameter local_vp 1 (1.0, 0.0, 0.0, 1.0)",SR_P_LOCAL_VP,0,NULL,{1,1.0,0.0,0.0,1.0}},
{23,"parameter local_vp 2 (0.99999, 0.0, 0.0, 0.0)",SR_P_LOCAL_VP,0,NULL,{2,0.99999,0.0,0.0,0.0}},
{24,"clear color 1.0 0.0 1.0 0.0",SR_CLEARCOLOR,0,NULL,{1.0,0.0,1.0,0.0}},
{25,"clear",SR_CLEAR,0,NULL,{0}},
{26,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{27,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{15,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{16,"color 0.25 0.75 0.5 0.25",SR_COLOR,0,NULL,{0.25,0.75,0.5,0.25}},
{17,"draw rect tex -0.25 -0.25 0.5 0.5 0 0 1 1",SR_QUADT,0,NULL,{-0.25,-0.25,0.5,0.5,0,0,1,1}},
{18,"relative probe rgba (0.5, 0.5) (0.25, 0.75, 0.5, 0.25)",SR_REL4,0,NULL,{0.5,0.5,0.25,0.75,0.5,0.25}},
{31,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{32,"texcoord 0 (-1, 2, 0, 0)",SR_TCOORD,0,NULL,{0,-1,2,0,0}},
{33,"parameter local_vp 0 (0, 0.5, -1.3, 0.1)",SR_P_LOCAL_VP,0,NULL,{0,0,0.5,-1.3,0.1}},
{34,"parameter local_vp 1 (-0.3, 0, 0.4, -0.5)",SR_P_LOCAL_VP,0,NULL,{1,-0.3,0,0.4,-0.5}},
{35,"parameter local_vp 2 (0.2, 0.3, 0, 0.2)",SR_P_LOCAL_VP,0,NULL,{2,0.2,0.3,0,0.2}},
{36,"parameter local_vp 3 (-0.1, 0.1, -0.1, 0)",SR_P_LOCAL_VP,0,NULL,{3,-0.1,0.1,-0.1,0}},
{37,"clear color 1.0 1.0 0.0 0.0",SR_CLEARCOLOR,0,NULL,{1.0,1.0,0.0,0.0}},
{38,"clear",SR_CLEAR,0,NULL,{0}},
{39,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{40,"probe all rgba 0.5 0.3 0.7 0.8",SR_ALL4,0,NULL,{0.5,0.3,0.7,0.8}},
{16,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{17,"parameter local_vp 0 (0.0, 0.0, 0.0, 0.0)",SR_P_LOCAL_VP,0,NULL,{0,0.0,0.0,0.0,0.0}},
{18,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{22,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{23,"texcoord 0 (0.4, -0.3, 0.1, 0.5)",SR_TCOORD,0,NULL,{0,0.4,-0.3,0.1,0.5}},
{24,"texcoord 1 (-1, 1, 8, 1)",SR_TCOORD,0,NULL,{1,-1,1,8,1}},
{25,"clear color 1.0 1.0 1.0 0.0",SR_CLEARCOLOR,0,NULL,{1.0,1.0,1.0,0.0}},
{26,"clear",SR_CLEAR,0,NULL,{0}},
{27,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{28,"probe all rgba 0.1 0 0 1",SR_ALL4,0,NULL,{0.1,0,0,1}},
{22,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{23,"texcoord 0 (0.4, -0.3, 0.1, 0.5)",SR_TCOORD,0,NULL,{0,0.4,-0.3,0.1,0.5}},
{24,"texcoord 1 (-1, 1, 8, 0.5)",SR_TCOORD,0,NULL,{1,-1,1,8,0.5}},
{25,"clear color 1.0 1.0 1.0 0.0",SR_CLEARCOLOR,0,NULL,{1.0,1.0,1.0,0.0}},
{26,"clear",SR_CLEAR,0,NULL,{0}},
{27,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{28,"probe all rgba 0.35 0 0 1",SR_ALL4,0,NULL,{0.35,0,0,1}},
{22,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{23,"texcoord 0 (0.4, -0.3, 0.1, 0.5)",SR_TCOORD,0,NULL,{0,0.4,-0.3,0.1,0.5}},
{24,"texcoord 1 (-1, 1, 8, 0.5)",SR_TCOORD,0,NULL,{1,-1,1,8,0.5}},
{25,"clear color 0.0 1.0 1.0 0.0",SR_CLEARCOLOR,0,NULL,{0.0,1.0,1.0,0.0}},
{26,"clear",SR_CLEAR,0,NULL,{0}},
{27,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{28,"probe all rgba 0.6 0 0 1",SR_ALL4,0,NULL,{0.6,0,0,1}},
{14,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{15,"parameter local_vp 0 (0.0, 0.0, 0.0, 0.0)",SR_P_LOCAL_VP,0,NULL,{0,0.0,0.0,0.0,0.0}},
{16,"parameter local_vp 9 (INF, -INF, NAN, 0.0)",SR_P_LOCAL_VP,0,NULL,{9,__builtin_inff(),-__builtin_inff(),NAN,0.0}},
{17,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{13,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{14,"parameter local_vp 2 (-1.0, 0.0, 0.25, -0.5)",SR_P_LOCAL_VP,0,NULL,{2,-1.0,0.0,0.25,-0.5}},
{15,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{16,"relative probe rgba (0.5, 0.5) (1.0, 0.0, 0.25, 0.5)",SR_REL4,0,NULL,{0.5,0.5,1.0,0.0,0.25,0.5}},
{13,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{14,"color 0.25 0.75 0.5 0.25",SR_COLOR,0,NULL,{0.25,0.75,0.5,0.25}},
{15,"parameter local_vp 1 (0.5, 0.25, 0.9, 0.5)",SR_P_LOCAL_VP,0,NULL,{1,0.5,0.25,0.9,0.5}},
{16,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{17,"relative probe rgba (0.5, 0.5) (0.75, 1.0, 1.0, 0.75)",SR_REL4,0,NULL,{0.5,0.5,0.75,1.0,1.0,0.75}},
{33,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{34,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{35,"relative probe rgba (0.5, 0.5) (0.11, 0.22, 0.33, 0.44)",SR_REL4,0,NULL,{0.5,0.5,0.11,0.22,0.33,0.44}},
{16,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{17,"color 0.25 0.75 0.5 0.25",SR_COLOR,0,NULL,{0.25,0.75,0.5,0.25}},
{18,"parameter local_vp 2 (-1.0, 0.0, 0.25, -0.5)",SR_P_LOCAL_VP,0,NULL,{2,-1.0,0.0,0.25,-0.5}},
{19,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{20,"relative probe rgba (0.5, 0.5) (0.375, 0.375, 0.375, 0.375)",SR_REL4,0,NULL,{0.5,0.5,0.375,0.375,0.375,0.375}},
{16,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{17,"color 0.25 0.75 0.5 0.25",SR_COLOR,0,NULL,{0.25,0.75,0.5,0.25}},
{18,"parameter local_vp 2 (-1.0, 0.0, 0.25, -0.5)",SR_P_LOCAL_VP,0,NULL,{2,-1.0,0.0,0.25,-0.5}},
{19,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{20,"relative probe rgba (0.5, 0.5) (0.25, 0.25, 0.25, 0.25)",SR_REL4,0,NULL,{0.5,0.5,0.25,0.25,0.25,0.25}},
{14,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{15,"color 0.25 0.75 0.5 0.25",SR_COLOR,0,NULL,{0.25,0.75,0.5,0.25}},
{16,"parameter local_vp 2 (-1.0, 0.0, 0.25, -0.5)",SR_P_LOCAL_VP,0,NULL,{2,-1.0,0.0,0.25,-0.5}},
{17,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{18,"relative probe rgba (0.5, 0.5) (0.125, 0.125, 0.125, 0.125)",SR_REL4,0,NULL,{0.5,0.5,0.125,0.125,0.125,0.125}},
{15,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{16,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{18,"relative probe rgba (0.5, 0.5) (1.0, 0.4, 0.16, 1.0)",SR_REL4,0,NULL,{0.5,0.5,1.0,0.4,0.16,1.0}},
{19,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{20,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{21,"relative probe rgba (0.5, 0.5) (0.01, 0.02, 0.16, 0.0025)",SR_REL4,0,NULL,{0.5,0.5,0.01,0.02,0.16,0.0025}},
{16,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{17,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{18,"relative probe rgba (0.5, 0.5) (0.16, 0.005, 0.22627, 0.01)",SR_REL4,0,NULL,{0.5,0.5,0.16,0.005,0.22627,0.01}},
{16,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{17,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{18,"relative probe rgba (0.5, 0.5) (0.4, 0.0, 0.0, 0.1)",SR_REL4,0,NULL,{0.5,0.5,0.4,0.0,0.0,0.1}},
{13,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{14,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{15,"relative probe rgba (0.5, 0.5) (0.344, 0.5, 0.9, 0.2)",SR_REL4,0,NULL,{0.5,0.5,0.344,0.5,0.9,0.2}},
{19,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{20,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{21,"relative probe rgba (0.5, 0.5) (0.6, 0.0, 0.49, 0.2)",SR_REL4,0,NULL,{0.5,0.5,0.6,0.0,0.49,0.2}},
{14,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{15,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{17,"relative probe rgba (0.5, 0.5) (1.0, 0.65, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.5,1.0,0.65,1.0,1.0}},
{13,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{14,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{16,"relative probe rgba (0.5, 0.5) (1.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.5,0.5,1.0,0.0,0.0,1.0}},
{19,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{20,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{22,"relative probe rgba (0.5, 0.5) (0.6, 0.15, 0.49, 0.1)",SR_REL4,0,NULL,{0.5,0.5,0.6,0.15,0.49,0.1}},
{14,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{15,"color 0.25 0.75 0.5 0.25",SR_COLOR,0,NULL,{0.25,0.75,0.5,0.25}},
{16,"parameter local_vp 1 (0.5, 0.25, 0.9, 0.5)",SR_P_LOCAL_VP,0,NULL,{1,0.5,0.25,0.9,0.5}},
{17,"parameter local_vp 2 (-1.0, 0.0, 0.25, -0.5)",SR_P_LOCAL_VP,0,NULL,{2,-1.0,0.0,0.25,-0.5}},
{18,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{19,"relative probe rgba (0.5, 0.5) (0.0, 0.1875, 0.7, 0.0)",SR_REL4,0,NULL,{0.5,0.5,0.0,0.1875,0.7,0.0}},
{14,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{15,"parameter local_vp 1 (0.5, 0.25, 0.9, 0.5)",SR_P_LOCAL_VP,0,NULL,{1,0.5,0.25,0.9,0.5}},
{16,"parameter local_vp 2 (-1.0, 0.0, 0.25, -0.5)",SR_P_LOCAL_VP,0,NULL,{2,-1.0,0.0,0.25,-0.5}},
{17,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{18,"relative probe rgba (0.5, 0.5) (0.5, 0.25, 0.9, 0.5)",SR_REL4,0,NULL,{0.5,0.5,0.5,0.25,0.9,0.5}},
{13,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{14,"color 0.25 0.75 0.5 0.25",SR_COLOR,0,NULL,{0.25,0.75,0.5,0.25}},
{15,"parameter local_vp 1 (0.5, 0.25, 0.9, 0.5)",SR_P_LOCAL_VP,0,NULL,{1,0.5,0.25,0.9,0.5}},
{16,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{17,"relative probe rgba (0.5, 0.5) (0.25, 0.25, 0.5, 0.25)",SR_REL4,0,NULL,{0.5,0.5,0.25,0.25,0.5,0.25}},
{12,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{13,"color 0.25 0.75 0.5 0.25",SR_COLOR,0,NULL,{0.25,0.75,0.5,0.25}},
{14,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{15,"relative probe rgba (0.5, 0.5) (0.25, 0.5, 0.25, 0.75)",SR_REL4,0,NULL,{0.5,0.5,0.25,0.5,0.25,0.75}},
{14,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{15,"color 0.25 0.75 0.5 0.25",SR_COLOR,0,NULL,{0.25,0.75,0.5,0.25}},
{16,"parameter local_vp 1 (0.5, 0.25, 0.9, 0.5)",SR_P_LOCAL_VP,0,NULL,{1,0.5,0.25,0.9,0.5}},
{17,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{18,"relative probe rgba (0.5, 0.5) (0.125, 0.45, 0.1875, 0.125)",SR_REL4,0,NULL,{0.5,0.5,0.125,0.45,0.1875,0.125}},
{16,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{17,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{18,"relative probe rgba (0.5, 0.5) (0.25, 0.125, 0.0625, 1.0)",SR_REL4,0,NULL,{0.5,0.5,0.25,0.125,0.0625,1.0}},
{16,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{17,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{18,"relative probe rgba (0.5, 0.5) (0.125, 0.0, 1, 0.08333333333333333)",SR_REL4,0,NULL,{0.5,0.5,0.125,0.0,1,0.08333333333333333}},
{16,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{17,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{18,"relative probe rgba (0.5, 0.5) (1.0, 0.5, 0.3333, 0.1)",SR_REL4,0,NULL,{0.5,0.5,1.0,0.5,0.3333,0.1}},
{16,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{17,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{18,"relative probe rgb (0.5, 0.5) (0.1, 0.447, 1.0)",SR_REL3,0,NULL,{0.5,0.5,0.1,0.447,1.0}},
{14,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{15,"parameter local_vp 0 (0.0, 0.0, 0.0, 0.0)",SR_P_LOCAL_VP,0,NULL,{0,0.0,0.0,0.0,0.0}},
{16,"parameter local_vp 2 (-1.0, 0.0, 0.25, -0.5)",SR_P_LOCAL_VP,0,NULL,{2,-1.0,0.0,0.25,-0.5}},
{17,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{18,"relative probe rgba (0.5, 0.5) (0.0, 1.0, 1.0, 0.0)",SR_REL4,0,NULL,{0.5,0.5,0.0,1.0,1.0,0.0}},
{14,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{15,"parameter local_vp 0 (0.0, 0.0, 0.0, 0.0)",SR_P_LOCAL_VP,0,NULL,{0,0.0,0.0,0.0,0.0}},
{16,"parameter local_vp 2 (-1.0, 0.0, 0.25, -0.5)",SR_P_LOCAL_VP,0,NULL,{2,-1.0,0.0,0.25,-0.5}},
{17,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{18,"relative probe rgba (0.5, 0.5) (1.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.5,0.5,1.0,0.0,0.0,1.0}},
{13,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{14,"color 0.25 0.75 0.5 0.25",SR_COLOR,0,NULL,{0.25,0.75,0.5,0.25}},
{15,"parameter local_vp 1 (0.5, 0.25, 0.9, 0.5)",SR_P_LOCAL_VP,0,NULL,{1,0.5,0.25,0.9,0.5}},
{16,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{17,"relative probe rgba (0.5, 0.5) (0.0, 0.25, 0.25, 0.4)",SR_REL4,0,NULL,{0.5,0.5,0.0,0.25,0.25,0.4}},
{13,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{14,"parameter local_vp 1 (0.5, 0.25, 0.9, 0.5)",SR_P_LOCAL_VP,0,NULL,{1,0.5,0.25,0.9,0.5}},
{15,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{16,"relative probe rgba (0.5, 0.5) (0.5, 0.5, 0.5, 0.25)",SR_REL4,0,NULL,{0.5,0.5,0.5,0.5,0.5,0.25}},
{13,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{14,"parameter local_vp 1 (0.5, 0.25, 0.9, 0.5)",SR_P_LOCAL_VP,0,NULL,{1,0.5,0.25,0.9,0.5}},
{15,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{16,"relative probe rgba (0.5, 0.5) (0.0, 1.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.5,0.5,0.0,1.0,0.0,1.0}},
{13,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{14,"parameter local_vp 1 (0.5, 0.25, 0.9, 0.5)",SR_P_LOCAL_VP,0,NULL,{1,0.5,0.25,0.9,0.5}},
{15,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{16,"relative probe rgba (0.5, 0.5) (0.0, 0.0, 0.5, 0.25)",SR_REL4,0,NULL,{0.5,0.5,0.0,0.0,0.5,0.25}},
{13,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{14,"parameter local_vp 1 (0.5, 0.25, 0.9, 0.5)",SR_P_LOCAL_VP,0,NULL,{1,0.5,0.25,0.9,0.5}},
{15,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{16,"relative probe rgba (0.5, 0.5) (1.0, 0.5, 0.9, 0.0)",SR_REL4,0,NULL,{0.5,0.5,1.0,0.5,0.9,0.0}},
{13,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{14,"parameter local_vp 1 (0.5, 0.25, 0.9, 0.5)",SR_P_LOCAL_VP,0,NULL,{1,0.5,0.25,0.9,0.5}},
{15,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{16,"relative probe rgba (0.5, 0.5) (0.9, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.5,0.5,0.9,0.0,0.0,0.0}},
{14,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{15,"parameter local_vp 1 (0.5, 0.25, 0.9, 0.5)",SR_P_LOCAL_VP,0,NULL,{1,0.5,0.25,0.9,0.5}},
{16,"parameter local_vp 2 (-1.0, 0.0, 0.25, -0.5)",SR_P_LOCAL_VP,0,NULL,{2,-1.0,0.0,0.25,-0.5}},
{17,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{18,"relative probe rgb (0.5, 0.5) (0.0625, 0.0, 0.25)",SR_REL3,0,NULL,{0.5,0.5,0.0625,0.0,0.25}},
{17,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{18,"parameter local_vp 1 (0.5, 0.25, 0.9, 0.5)",SR_P_LOCAL_VP,0,NULL,{1,0.5,0.25,0.9,0.5}},
{19,"parameter local_vp 2 (-1.0, 0.0, 0.25, -0.5)",SR_P_LOCAL_VP,0,NULL,{2,-1.0,0.0,0.25,-0.5}},
{20,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{21,"relative probe rgb (0.5, 0.5) (0.0625, 0.0, 0.25)",SR_REL3,0,NULL,{0.5,0.5,0.0625,0.0,0.25}},
{138,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{139,"parameter local_vp 0 (1.0, 0.0, 0.0, 0.0)",SR_P_LOCAL_VP,0,NULL,{0,1.0,0.0,0.0,0.0}},
{140,"clear color 1.0 0.0 0.0 1.0",SR_CLEARCOLOR,0,NULL,{1.0,0.0,0.0,1.0}},
{141,"clear",SR_CLEAR,0,NULL,{0}},
{142,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{143,"probe all rgba 0.5 1.0 1.0 0.0",SR_ALL4,0,NULL,{0.5,1.0,1.0,0.0}},
{144,"parameter local_vp 0 (2.0, 0.0, 0.0, 0.0)",SR_P_LOCAL_VP,0,NULL,{0,2.0,0.0,0.0,0.0}},
{145,"clear color 0.0 1.0 0.0 1.0",SR_CLEARCOLOR,0,NULL,{0.0,1.0,0.0,1.0}},
{146,"clear",SR_CLEAR,0,NULL,{0}},
{147,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{148,"probe all rgba 1.0 0.0 0.8 0.0",SR_ALL4,0,NULL,{1.0,0.0,0.8,0.0}},
{149,"parameter local_vp 0 (3.0, 0.0, 0.0, 0.0)",SR_P_LOCAL_VP,0,NULL,{0,3.0,0.0,0.0,0.0}},
{150,"clear color 0.0 1.0 1.0 1.0",SR_CLEARCOLOR,0,NULL,{0.0,1.0,1.0,1.0}},
{151,"clear",SR_CLEAR,0,NULL,{0}},
{152,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{153,"probe all rgba 1.0 0.0 0.5 0.0",SR_ALL4,0,NULL,{1.0,0.0,0.5,0.0}},
{154,"parameter local_vp 0 (4.0, 0.0, 0.0, 0.0)",SR_P_LOCAL_VP,0,NULL,{0,4.0,0.0,0.0,0.0}},
{155,"clear color 0.0 0.0 1.0 1.0",SR_CLEARCOLOR,0,NULL,{0.0,0.0,1.0,1.0}},
{156,"clear",SR_CLEAR,0,NULL,{0}},
{157,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{158,"probe all rgba 0.8 1.0 0.0 0.0",SR_ALL4,0,NULL,{0.8,1.0,0.0,0.0}},
{137,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{138,"parameter local_vp 0 (-1.0, 0.0, 0.0, 0.0)",SR_P_LOCAL_VP,0,NULL,{0,-1.0,0.0,0.0,0.0}},
{139,"clear color 1.0 0.0 0.0 1.0",SR_CLEARCOLOR,0,NULL,{1.0,0.0,0.0,1.0}},
{140,"clear",SR_CLEAR,0,NULL,{0}},
{141,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{142,"probe all rgba 0.5 1.0 1.0 0.0",SR_ALL4,0,NULL,{0.5,1.0,1.0,0.0}},
{143,"parameter local_vp 0 (0.0, 0.0, 0.0, 0.0)",SR_P_LOCAL_VP,0,NULL,{0,0.0,0.0,0.0,0.0}},
{144,"clear color 0.0 1.0 0.0 1.0",SR_CLEARCOLOR,0,NULL,{0.0,1.0,0.0,1.0}},
{145,"clear",SR_CLEAR,0,NULL,{0}},
{146,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{147,"probe all rgba 1.0 0.0 0.8 0.0",SR_ALL4,0,NULL,{1.0,0.0,0.8,0.0}},
{148,"parameter local_vp 0 (1.0, 0.0, 0.0, 0.0)",SR_P_LOCAL_VP,0,NULL,{0,1.0,0.0,0.0,0.0}},
{149,"clear color 0.0 1.0 1.0 1.0",SR_CLEARCOLOR,0,NULL,{0.0,1.0,1.0,1.0}},
{150,"clear",SR_CLEAR,0,NULL,{0}},
{151,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{152,"probe all rgba 1.0 0.0 0.5 0.0",SR_ALL4,0,NULL,{1.0,0.0,0.5,0.0}},
{153,"parameter local_vp 0 (2.0, 0.0, 0.0, 0.0)",SR_P_LOCAL_VP,0,NULL,{0,2.0,0.0,0.0,0.0}},
{154,"clear color 0.0 0.0 1.0 1.0",SR_CLEARCOLOR,0,NULL,{0.0,0.0,1.0,1.0}},
{155,"clear",SR_CLEAR,0,NULL,{0}},
{156,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{157,"probe all rgba 0.8 1.0 0.0 0.0",SR_ALL4,0,NULL,{0.8,1.0,0.0,0.0}},
{141,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{142,"parameter local_vp 0 (1.0, 0.0, 0.0, 0.0)",SR_P_LOCAL_VP,0,NULL,{0,1.0,0.0,0.0,0.0}},
{143,"clear color 1.0 0.0 0.0 1.0",SR_CLEARCOLOR,0,NULL,{1.0,0.0,0.0,1.0}},
{144,"clear",SR_CLEAR,0,NULL,{0}},
{145,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{146,"probe all rgba 0.5 1.0 1.0 0.0",SR_ALL4,0,NULL,{0.5,1.0,1.0,0.0}},
{137,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{138,"parameter local_vp 0 (-1.0, 0.0, 0.0, 0.0)",SR_P_LOCAL_VP,0,NULL,{0,-1.0,0.0,0.0,0.0}},
{139,"clear color 1.0 0.0 0.0 1.0",SR_CLEARCOLOR,0,NULL,{1.0,0.0,0.0,1.0}},
{140,"clear",SR_CLEAR,0,NULL,{0}},
{141,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{142,"probe all rgba 0.5 1.0 1.0 0.0",SR_ALL4,0,NULL,{0.5,1.0,1.0,0.0}},
{143,"parameter local_vp 0 (0.0, 0.0, 0.0, 0.0)",SR_P_LOCAL_VP,0,NULL,{0,0.0,0.0,0.0,0.0}},
{144,"clear color 0.0 1.0 0.0 1.0",SR_CLEARCOLOR,0,NULL,{0.0,1.0,0.0,1.0}},
{145,"clear",SR_CLEAR,0,NULL,{0}},
{146,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{147,"probe all rgba 1.0 0.0 0.8 0.0",SR_ALL4,0,NULL,{1.0,0.0,0.8,0.0}},
{148,"parameter local_vp 0 (1.0, 0.0, 0.0, 0.0)",SR_P_LOCAL_VP,0,NULL,{0,1.0,0.0,0.0,0.0}},
{149,"clear color 0.0 1.0 1.0 1.0",SR_CLEARCOLOR,0,NULL,{0.0,1.0,1.0,1.0}},
{150,"clear",SR_CLEAR,0,NULL,{0}},
{151,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{152,"probe all rgba 1.0 0.0 0.5 0.0",SR_ALL4,0,NULL,{1.0,0.0,0.5,0.0}},
{153,"parameter local_vp 0 (2.0, 0.0, 0.0, 0.0)",SR_P_LOCAL_VP,0,NULL,{0,2.0,0.0,0.0,0.0}},
{154,"clear color 0.0 0.0 1.0 1.0",SR_CLEARCOLOR,0,NULL,{0.0,0.0,1.0,1.0}},
{155,"clear",SR_CLEAR,0,NULL,{0}},
{156,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{157,"probe all rgba 0.8 1.0 0.0 0.0",SR_ALL4,0,NULL,{0.8,1.0,0.0,0.0}},
{159,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{160,"clear color 0.0 1.0 1.0 1.0",SR_CLEARCOLOR,0,NULL,{0.0,1.0,1.0,1.0}},
{161,"clear",SR_CLEAR,0,NULL,{0}},
{162,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{163,"probe all rgba 0.75 0.25 0.0 0.0",SR_ALL4,0,NULL,{0.75,0.25,0.0,0.0}},
{139,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{140,"parameter local_vp 0 (0.0, 0.0, 0.0, 0.0)",SR_P_LOCAL_VP,0,NULL,{0,0.0,0.0,0.0,0.0}},
{141,"clear color 1.0 0.0 0.0 1.0",SR_CLEARCOLOR,0,NULL,{1.0,0.0,0.0,1.0}},
{142,"clear",SR_CLEAR,0,NULL,{0}},
{143,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{144,"probe all rgba 0.5 1.0 1.0 0.0",SR_ALL4,0,NULL,{0.5,1.0,1.0,0.0}},
{145,"parameter local_vp 0 (1.0, 0.0, 0.0, 0.0)",SR_P_LOCAL_VP,0,NULL,{0,1.0,0.0,0.0,0.0}},
{146,"clear color 0.0 1.0 0.0 1.0",SR_CLEARCOLOR,0,NULL,{0.0,1.0,0.0,1.0}},
{147,"clear",SR_CLEAR,0,NULL,{0}},
{148,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{149,"probe all rgba 1.0 0.0 0.8 0.0",SR_ALL4,0,NULL,{1.0,0.0,0.8,0.0}},
{150,"parameter local_vp 0 (2.0, 0.0, 0.0, 0.0)",SR_P_LOCAL_VP,0,NULL,{0,2.0,0.0,0.0,0.0}},
{151,"clear color 0.0 1.0 1.0 1.0",SR_CLEARCOLOR,0,NULL,{0.0,1.0,1.0,1.0}},
{152,"clear",SR_CLEAR,0,NULL,{0}},
{153,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{154,"probe all rgba 1.0 0.0 0.5 0.0",SR_ALL4,0,NULL,{1.0,0.0,0.5,0.0}},
{155,"parameter local_vp 0 (3.0, 0.0, 0.0, 0.0)",SR_P_LOCAL_VP,0,NULL,{0,3.0,0.0,0.0,0.0}},
{156,"clear color 0.0 0.0 1.0 1.0",SR_CLEARCOLOR,0,NULL,{0.0,0.0,1.0,1.0}},
{157,"clear",SR_CLEAR,0,NULL,{0}},
{158,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{159,"probe all rgba 0.8 1.0 0.0 0.0",SR_ALL4,0,NULL,{0.8,1.0,0.0,0.0}},
{25,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{26,"clear color 0.0 1.0 1.0 1.0",SR_CLEARCOLOR,0,NULL,{0.0,1.0,1.0,1.0}},
{27,"clear",SR_CLEAR,0,NULL,{0}},
{28,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{29,"probe all rgba 0.75 0.25 0.0 0.0",SR_ALL4,0,NULL,{0.75,0.25,0.0,0.0}},
{22,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{23,"parameter local_vp 0 (3.0, 0.0, 0.0, 0.0)",SR_P_LOCAL_VP,0,NULL,{0,3.0,0.0,0.0,0.0}},
{24,"clear color 0.0 0.0 1.0 1.0",SR_CLEARCOLOR,0,NULL,{0.0,0.0,1.0,1.0}},
{25,"clear",SR_CLEAR,0,NULL,{0}},
{26,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{27,"probe all rgba 0.8 1.0 0.0 0.0",SR_ALL4,0,NULL,{0.8,1.0,0.0,0.0}},
{17,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{18,"parameter local_vp 0 (3.0, 0.0, 0.0, 0.0)",SR_P_LOCAL_VP,0,NULL,{0,3.0,0.0,0.0,0.0}},
{19,"parameter env_vp 1 (0.5, 1.0, 2.0, 0.0)",SR_P_ENV_VP,0,NULL,{1,0.5,1.0,2.0,0.0}},
{20,"parameter env_vp 2 (1.0, 0.0, 1.0, 0.0)",SR_P_ENV_VP,0,NULL,{2,1.0,0.0,1.0,0.0}},
{21,"parameter env_vp 3 (1.0, 0.0, 0.5, 0.0)",SR_P_ENV_VP,0,NULL,{3,1.0,0.0,0.5,0.0}},
{22,"parameter env_vp 4 (0.8, 1.0, 0.0, 0.0)",SR_P_ENV_VP,0,NULL,{4,0.8,1.0,0.0,0.0}},
{23,"clear color 0.0 0.0 1.0 1.0",SR_CLEARCOLOR,0,NULL,{0.0,0.0,1.0,1.0}},
{24,"clear",SR_CLEAR,0,NULL,{0}},
{25,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{26,"probe all rgba 0.8 1.0 0.0 0.0",SR_ALL4,0,NULL,{0.8,1.0,0.0,0.0}},
{17,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{18,"parameter local_vp 0 (3.0, 0.0, 0.0, 0.0)",SR_P_LOCAL_VP,0,NULL,{0,3.0,0.0,0.0,0.0}},
{19,"parameter local_vp 1 (0.5, 1.0, 2.0, 0.0)",SR_P_LOCAL_VP,0,NULL,{1,0.5,1.0,2.0,0.0}},
{20,"parameter local_vp 2 (1.0, 0.0, 1.0, 0.0)",SR_P_LOCAL_VP,0,NULL,{2,1.0,0.0,1.0,0.0}},
{21,"parameter local_vp 3 (1.0, 0.0, 0.5, 0.0)",SR_P_LOCAL_VP,0,NULL,{3,1.0,0.0,0.5,0.0}},
{22,"parameter local_vp 4 (0.8, 1.0, 0.0, 0.0)",SR_P_LOCAL_VP,0,NULL,{4,0.8,1.0,0.0,0.0}},
{23,"clear color 0.0 0.0 1.0 1.0",SR_CLEARCOLOR,0,NULL,{0.0,0.0,1.0,1.0}},
{24,"clear",SR_CLEAR,0,NULL,{0}},
{25,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{26,"probe all rgba 0.8 1.0 0.0 0.0",SR_ALL4,0,NULL,{0.8,1.0,0.0,0.0}},
{23,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{24,"parameter local_vp 0 (-3, 0.0, 0.0, 0.0)",SR_P_LOCAL_VP,0,NULL,{0,-3,0.0,0.0,0.0}},
{25,"clear color 0.0 0.0 1.0 1.0",SR_CLEARCOLOR,0,NULL,{0.0,0.0,1.0,1.0}},
{26,"clear",SR_CLEAR,0,NULL,{0}},
{27,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{28,"probe all rgba 0.8 1.0 0.0 0.0",SR_ALL4,0,NULL,{0.8,1.0,0.0,0.0}},
{23,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{24,"parameter local_vp 0 (6, 0.0, 0.0, 0.0)",SR_P_LOCAL_VP,0,NULL,{0,6,0.0,0.0,0.0}},
{25,"clear color 0.0 0.0 1.0 1.0",SR_CLEARCOLOR,0,NULL,{0.0,0.0,1.0,1.0}},
{26,"clear",SR_CLEAR,0,NULL,{0}},
{27,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{28,"probe all rgba 0.8 1.0 0.0 0.0",SR_ALL4,0,NULL,{0.8,1.0,0.0,0.0}},
{137,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{138,"parameter local_vp 0 (3.0, 0.0, 0.0, 0.0)",SR_P_LOCAL_VP,0,NULL,{0,3.0,0.0,0.0,0.0}},
{139,"clear color 0.0 0.0 1.0 1.0",SR_CLEARCOLOR,0,NULL,{0.0,0.0,1.0,1.0}},
{140,"clear",SR_CLEAR,0,NULL,{0}},
{141,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{142,"probe all rgba 0.8 1.0 0.0 0.0",SR_ALL4,0,NULL,{0.8,1.0,0.0,0.0}},
{19,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{20,"clear color 0.0 0.0 1.0 1.0",SR_CLEARCOLOR,0,NULL,{0.0,0.0,1.0,1.0}},
{21,"clear",SR_CLEAR,0,NULL,{0}},
{22,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{23,"probe all rgba 0.8 1.0 0.0 0.0",SR_ALL4,0,NULL,{0.8,1.0,0.0,0.0}},
{22,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{23,"clear color 1.0 0.0 1.0 1.0",SR_CLEARCOLOR,0,NULL,{1.0,0.0,1.0,1.0}},
{24,"clear",SR_CLEAR,0,NULL,{0}},
{25,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{26,"probe all rgba 0.0 1.0 0.0 0.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,0.0}},
{20,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{21,"clear color 1.0 1.0 1.0 1.0",SR_CLEARCOLOR,0,NULL,{1.0,1.0,1.0,1.0}},
{22,"clear",SR_CLEAR,0,NULL,{0}},
{23,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{24,"probe all rgba 0.5 0.5 0.5 0.5",SR_ALL4,0,NULL,{0.5,0.5,0.5,0.5}},
{21,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{22,"clear color 1.0 1.0 1.0 1.0",SR_CLEARCOLOR,0,NULL,{1.0,1.0,1.0,1.0}},
{23,"clear",SR_CLEAR,0,NULL,{0}},
{24,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{25,"probe all rgba 0.25 0.25 0.25 0.25",SR_ALL4,0,NULL,{0.25,0.25,0.25,0.25}},
{21,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{22,"clear color 1.0 1.0 1.0 1.0",SR_CLEARCOLOR,0,NULL,{1.0,1.0,1.0,1.0}},
{23,"clear",SR_CLEAR,0,NULL,{0}},
{24,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{25,"probe all rgba 0.25 0.25 0.25 0.25",SR_ALL4,0,NULL,{0.25,0.25,0.25,0.25}},
{18,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{19,"clear color 0.0 1.0 0.0 0.0",SR_CLEARCOLOR,0,NULL,{0.0,1.0,0.0,0.0}},
{20,"clear",SR_CLEAR,0,NULL,{0}},
{21,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{22,"probe all rgba 1 0 1 1",SR_ALL4,0,NULL,{1,0,1,1}},
{18,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{19,"clear color 0.0 1.0 0.0 1.0",SR_CLEARCOLOR,0,NULL,{0.0,1.0,0.0,1.0}},
{20,"clear",SR_CLEAR,0,NULL,{0}},
{21,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{22,"probe all rgba 1.0 0.0 1.0 0.0",SR_ALL4,0,NULL,{1.0,0.0,1.0,0.0}},
{15,"enable GL_DEPTH_TEST",SR_ENABLE,GL_DEPTH_TEST,NULL,{0}},
{16,"clear depth 1",SR_CLEARZ,0,NULL,{1}},
{17,"ortho -4 4 -4 4",SR_ORTHO,0,NULL,{-4,4,-4,4}},
{18,"color 0.25 0.75 0.5 0.25",SR_COLOR,0,NULL,{0.25,0.75,0.5,0.25}},
{19,"parameter local_vp 1 (0.5, 0.25, 0.9, 0.5)",SR_P_LOCAL_VP,0,NULL,{1,0.5,0.25,0.9,0.5}},
{20,"clear",SR_CLEAR,0,NULL,{0}},
{21,"draw rect -0.25 -0.25 0.5 0.5",SR_QUAD,0,NULL,{-0.25,-0.25,0.5,0.5}},
{22,"relative probe rgba (0.5, 0.5) (0.25, 0.75, 0.5, 0.25)",SR_REL4,0,NULL,{0.5,0.5,0.25,0.75,0.5,0.25}},
{23,"probe depth 125 125 0.625",SR_ZPIX,0,NULL,{125,125,0.625}},
{17,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{18,"relative probe rgba (0.5, 0.5) (0.75, 0.75, 0.75, 0.75)",SR_REL4,0,NULL,{0.5,0.5,0.75,0.75,0.75,0.75}},
{22,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{23,"relative probe rgba (0.5, 0.5) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.5,1.0,1.0,1.0,1.0}},
{14,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{15,"relative probe rgba (0.5, 0.5) (0.75, 0.75, 0.75, 0.75)",SR_REL4,0,NULL,{0.5,0.5,0.75,0.75,0.75,0.75}},
{26,"uniform float one 1.0",SR_U_FLOAT,0,"one",{1.0}},
{27,"uniform float zero 0.0",SR_U_FLOAT,0,"zero",{0.0}},
{28,"uniform int f 0",SR_U_INT,0,"f",{0}},
{30,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{31,"probe all rgba 0.0 1.0 0.0 0.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,0.0}},
{26,"uniform float one 1.0",SR_U_FLOAT,0,"one",{1.0}},
{27,"uniform float zero 0.0",SR_U_FLOAT,0,"zero",{0.0}},
{28,"uniform int t 1",SR_U_INT,0,"t",{1}},
{30,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{31,"probe all rgba 0.0 1.0 0.0 0.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,0.0}},
{36,"hint GL_FRAGMENT_SHADER_DERIVATIVE_HINT GL_NICEST",SR_HINT,GL_FRAGMENT_SHADER_DERIVATIVE_HINT,NULL,{0}},
{37,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{38,"probe warn all rgba 0.0 1.0 0.0 1.0",SR_WARN4,0,NULL,{0.0,1.0,0.0,1.0}},
{36,"hint GL_FRAGMENT_SHADER_DERIVATIVE_HINT GL_NICEST",SR_HINT,GL_FRAGMENT_SHADER_DERIVATIVE_HINT,NULL,{0}},
{37,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{38,"probe warn all rgba 0.0 1.0 0.0 1.0",SR_WARN4,0,NULL,{0.0,1.0,0.0,1.0}},
{28,"clear color 1.0 0.0 0.0 1.0",SR_CLEARCOLOR,0,NULL,{1.0,0.0,0.0,1.0}},
{29,"clear",SR_CLEAR,0,NULL,{0}},
{31,"uniform uint use_discard 1",SR_U_UINT,0,"use_discard",{1}},
{32,"uniform float boundary 20.0",SR_U_FLOAT,0,"boundary",{20.0}},
{33,"uniform vec4 color 0.0 1.0 0.0 1.0",SR_U_VEC4,0,"color",{0.0,1.0,0.0,1.0}},
{35,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{36,"probe rect rgba (0, 0, 10, 20) (1.0, 0.0, 0.0, 1.0)",SR_BOX4,0,NULL,{0,0,10,20,1.0,0.0,0.0,1.0}},
{37,"probe rect rgba (10, 0, 10, 20) (0.0, 1.0, 0.0, 1.0)",SR_BOX4,0,NULL,{10,0,10,20,0.0,1.0,0.0,1.0}},
{39,"uniform uint use_discard 0",SR_U_UINT,0,"use_discard",{0}},
{40,"uniform vec4 color 0.0 0.0 1.0 1.0",SR_U_VEC4,0,"color",{0.0,0.0,1.0,1.0}},
{41,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{42,"probe all rgba 0.0 0.0 1.0 1.0",SR_ALL4,0,NULL,{0.0,0.0,1.0,1.0}},
{31,"uniform vec4 a 1.0 1.0 1.0 1.0",SR_U_VEC4,0,"a",{1.0,1.0,1.0,1.0}},
{32,"uniform float b 38.2",SR_U_FLOAT,0,"b",{38.2}},
{34,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{35,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{15,"draw rect -1 -1 2 1",SR_QUAD,0,NULL,{-1,-1,2,1}},
{16,"draw rect 1 0 -2 1",SR_QUAD,0,NULL,{1,0,-2,1}},
{17,"relative probe rgba (0, 0) (0.0, 0.75, 0.0, 1.0)",SR_REL4,0,NULL,{0,0,0.0,0.75,0.0,1.0}},
{18,"relative probe rgba (1, 0) (0.0, 0.75, 0.0, 1.0)",SR_REL4,0,NULL,{1,0,0.0,0.75,0.0,1.0}},
{19,"relative probe rgba (0, 1) (0.0, 0.25, 0.0, 1.0)",SR_REL4,0,NULL,{0,1,0.0,0.25,0.0,1.0}},
{20,"relative probe rgba (1, 1) (0.0, 0.25, 0.0, 1.0)",SR_REL4,0,NULL,{1,1,0.0,0.25,0.0,1.0}},
{24,"uniform int front 1",SR_U_INT,0,"front",{1}},
{25,"uniform int back -1",SR_U_INT,0,"back",{-1}},
{27,"draw rect -1 -1 2 1",SR_QUAD,0,NULL,{-1,-1,2,1}},
{28,"draw rect 1 0 -2 1",SR_QUAD,0,NULL,{1,0,-2,1}},
{29,"relative probe rgba (0, 0) (0.0, 1.0, 0.0, 1.0)",SR_REL4,0,NULL,{0,0,0.0,1.0,0.0,1.0}},
{30,"relative probe rgba (1, 0) (0.0, 1.0, 0.0, 1.0)",SR_REL4,0,NULL,{1,0,0.0,1.0,0.0,1.0}},
{31,"relative probe rgba (0, 1) (0.0, 0.0, 1.0, 1.0)",SR_REL4,0,NULL,{0,1,0.0,0.0,1.0,1.0}},
{32,"relative probe rgba (1, 1) (0.0, 0.0, 1.0, 1.0)",SR_REL4,0,NULL,{1,1,0.0,0.0,1.0,1.0}},
{15,"draw rect -1 -1 2 1",SR_QUAD,0,NULL,{-1,-1,2,1}},
{16,"draw rect 1 0 -2 1",SR_QUAD,0,NULL,{1,0,-2,1}},
{17,"relative probe rgba (0, 0) (0.0, 0.75, 0.0, 1.0)",SR_REL4,0,NULL,{0,0,0.0,0.75,0.0,1.0}},
{18,"relative probe rgba (1, 0) (0.0, 0.75, 0.0, 1.0)",SR_REL4,0,NULL,{1,0,0.0,0.75,0.0,1.0}},
{19,"relative probe rgba (0, 1) (0.0, 0.25, 0.0, 1.0)",SR_REL4,0,NULL,{0,1,0.0,0.25,0.0,1.0}},
{20,"relative probe rgba (1, 1) (0.0, 0.25, 0.0, 1.0)",SR_REL4,0,NULL,{1,1,0.0,0.25,0.0,1.0}},
{15,"draw rect -1 -1 2 1",SR_QUAD,0,NULL,{-1,-1,2,1}},
{16,"draw rect 1 0 -2 1",SR_QUAD,0,NULL,{1,0,-2,1}},
{17,"relative probe rgba (0, 0) (0.0, 0.75, 0.0, 1.0)",SR_REL4,0,NULL,{0,0,0.0,0.75,0.0,1.0}},
{18,"relative probe rgba (1, 0) (0.0, 0.75, 0.0, 1.0)",SR_REL4,0,NULL,{1,0,0.0,0.75,0.0,1.0}},
{19,"relative probe rgba (0, 1) (0.0, 0.25, 0.0, 1.0)",SR_REL4,0,NULL,{0,1,0.0,0.25,0.0,1.0}},
{20,"relative probe rgba (1, 1) (0.0, 0.25, 0.0, 1.0)",SR_REL4,0,NULL,{1,1,0.0,0.25,0.0,1.0}},
{23,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{24,"probe all rgb 0.5 0.5 0.5",SR_ALL3,0,NULL,{0.5,0.5,0.5}},
{42,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{43,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{19,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{20,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{25,"clear color 0.2 0.2 0.2 0.2",SR_CLEARCOLOR,0,NULL,{0.2,0.2,0.2,0.2}},
{26,"clear",SR_CLEAR,0,NULL,{0}},
{28,"uniform float u 0.1",SR_U_FLOAT,0,"u",{0.1}},
{29,"uniform float u2 0.1",SR_U_FLOAT,0,"u2",{0.1}},
{31,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{32,"probe all rgba 0.0 0.35 0.0 1.0",SR_ALL4,0,NULL,{0.0,0.35,0.0,1.0}},
{25,"clear color 0.2 0.2 0.2 0.2",SR_CLEARCOLOR,0,NULL,{0.2,0.2,0.2,0.2}},
{26,"clear",SR_CLEAR,0,NULL,{0}},
{28,"uniform float u 0.75",SR_U_FLOAT,0,"u",{0.75}},
{29,"uniform float u2 0.75",SR_U_FLOAT,0,"u2",{0.75}},
{31,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{32,"probe all rgba 0.0 0.35 0.0 1.0",SR_ALL4,0,NULL,{0.0,0.35,0.0,1.0}},
{37,"uniform int a 1",SR_U_INT,0,"a",{1}},
{39,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{40,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{28,"clear color 0.5 0.5 0.5 0.5",SR_CLEARCOLOR,0,NULL,{0.5,0.5,0.5,0.5}},
{30,"uniform int a 1",SR_U_INT,0,"a",{1}},
{32,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{33,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{26,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{27,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{31,"uniform vec4 a 1.0 1.0 1.0 1.0",SR_U_VEC4,0,"a",{1.0,1.0,1.0,1.0}},
{32,"uniform float b 38.2",SR_U_FLOAT,0,"b",{38.2}},
{34,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{35,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{19,"uniform ivec2 arg0 38 35",SR_U_IVEC2,0,"arg0",{38,35}},
{20,"uniform ivec2 arg1 1 1",SR_U_IVEC2,0,"arg1",{1,1}},
{21,"uniform ivec2 expected 76 114",SR_U_IVEC2,0,"expected",{76,114}},
{22,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{23,"probe rgba 0 0 0.0 1.0 0.0 1.0",SR_PIX4,0,NULL,{0,0,0.0,1.0,0.0,1.0}},
{19,"uniform vec4 v -2 -1 1 2",SR_U_VEC4,0,"v",{-2,-1,1,2}},
{21,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{22,"probe all rgba 0.375 0.5 0.75 0.75",SR_ALL4,0,NULL,{0.375,0.5,0.75,0.75}},
{20,"uniform vec4 x 0.5 2.0 0.5 1.0",SR_U_VEC4,0,"x",{0.5,2.0,0.5,1.0}},
{21,"uniform vec4 y 2.0 0.5 -1.0 1.0",SR_U_VEC4,0,"y",{2.0,0.5,-1.0,1.0}},
{23,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{24,"probe all rgba 0.375 0.75 0.75 0.75",SR_ALL4,0,NULL,{0.375,0.75,0.75,0.75}},
{19,"uniform vec4 v 0.25 1.0 2.0 0.25",SR_U_VEC4,0,"v",{0.25,1.0,2.0,0.25}},
{21,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{22,"probe all rgba 0.5 0.75 0.75 0.5",SR_ALL4,0,NULL,{0.5,0.75,0.75,0.5}},
{23,"uniform float a -1",SR_U_FLOAT,0,"a",{-1}},
{24,"uniform float b 1",SR_U_FLOAT,0,"b",{1}},
{25,"uniform vec4 color0 1.0 -1.0 0.0 0.0",SR_U_VEC4,0,"color0",{1.0,-1.0,0.0,0.0}},
{26,"uniform vec4 color1 0.0 1.0 0.0 1.0",SR_U_VEC4,0,"color1",{0.0,1.0,0.0,1.0}},
{27,"draw rect -1 -1 1 2",SR_QUAD,0,NULL,{-1,-1,1,2}},
{28,"relative probe rgba (0.25, 0.5) (0.0, 1.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.25,0.5,0.0,1.0,0.0,1.0}},
{30,"uniform float a 1",SR_U_FLOAT,0,"a",{1}},
{31,"uniform float b 1",SR_U_FLOAT,0,"b",{1}},
{32,"uniform vec4 color0 -1.0 1.0 0.0 0.0",SR_U_VEC4,0,"color0",{-1.0,1.0,0.0,0.0}},
{33,"uniform vec4 color1 1.0 0.0 0.0 1.0",SR_U_VEC4,0,"color1",{1.0,0.0,0.0,1.0}},
{34,"draw rect 0 -1 1 2",SR_QUAD,0,NULL,{0,-1,1,2}},
{35,"relative probe rgba (0.75, 0.5) (0.0, 1.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.75,0.5,0.0,1.0,0.0,1.0}},
{28,"texture checkerboard 0 0 (8, 8) (0, 0, 0, 0) (0, 0, 0, 0)",SR_CHECKER,0,NULL,{0,0,8,8,0,0,0,0,0,0,0,0}},
{29,"texture checkerboard 1 0 (8, 8) (1, 1, 1, 1) (1, 1, 1, 1)",SR_CHECKER,0,NULL,{1,0,8,8,1,1,1,1,1,1,1,1}},
{31,"uniform int tex0 0",SR_U_INT,0,"tex0",{0}},
{32,"uniform int tex1 1",SR_U_INT,0,"tex1",{1}},
{33,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{34,"probe all rgba 1.0 1.0 1.0 1.0",SR_ALL4,0,NULL,{1.0,1.0,1.0,1.0}},
{38,"uniform int three 3",SR_U_INT,0,"three",{3}},
{39,"uniform float five 5.0",SR_U_FLOAT,0,"five",{5.0}},
{40,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{41,"probe all rgba 0.0 1.0 0.0 0.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,0.0}},
{71,"uniform int i 3",SR_U_INT,0,"i",{3}},
{73,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{74,"probe all rgba 0.2 0 0 1.0",SR_ALL4,0,NULL,{0.2,0,0,1.0}},
{71,"uniform int i 3",SR_U_INT,0,"i",{3}},
{73,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{74,"probe all rgba 0.2 0 0 1.0",SR_ALL4,0,NULL,{0.2,0,0,1.0}},
{69,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{70,"probe all rgba 0.2 0 0 1.0",SR_ALL4,0,NULL,{0.2,0,0,1.0}},
{69,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{70,"probe all rgba 0.2 0 0 1.0",SR_ALL4,0,NULL,{0.2,0,0,1.0}},
{63,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{64,"probe all rgba 0.2 0 0 1.0",SR_ALL4,0,NULL,{0.2,0,0,1.0}},
{63,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{64,"probe all rgba 0.2 0 0 1.0",SR_ALL4,0,NULL,{0.2,0,0,1.0}},
{63,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{64,"probe all rgba 0.2 0 0 1.0",SR_ALL4,0,NULL,{0.2,0,0,1.0}},
{59,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{60,"probe all rgba 0.2 0 0 1.0",SR_ALL4,0,NULL,{0.2,0,0,1.0}},
{63,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{64,"probe all rgba 0 0.2 0 1.0",SR_ALL4,0,NULL,{0,0.2,0,1.0}},
{63,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{64,"probe all rgba 0 0.2 0 1.0",SR_ALL4,0,NULL,{0,0.2,0,1.0}},
{15,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{16,"relative probe rgba (0.5, 0.5) (0.7, 1.0, 0.75, 0.4)",SR_REL4,0,NULL,{0.5,0.5,0.7,1.0,0.75,0.4}},
{13,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{14,"relative probe rgba (0.5, 0.5) (1.0, 0.5, 0.25, 0.0)",SR_REL4,0,NULL,{0.5,0.5,1.0,0.5,0.25,0.0}},
{19,"color 0.25 0.75 0.5 0.25",SR_COLOR,0,NULL,{0.25,0.75,0.5,0.25}},
{20,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{21,"relative probe rgba (0.5, 0.5) (0.925, 0.925, 0.6999, 0.575)",SR_REL4,0,NULL,{0.5,0.5,0.925,0.925,0.6999,0.575}},
{19,"color 0.25 0.75 0.5 0.25",SR_COLOR,0,NULL,{0.25,0.75,0.5,0.25}},
{20,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{21,"relative probe rgba (0.5, 0.5) (0.2745, 0.9255, 0.7294, 1.0)",SR_REL4,0,NULL,{0.5,0.5,0.2745,0.9255,0.7294,1.0}},
{14,"color 0.25 0.75 0.5 0.25",SR_COLOR,0,NULL,{0.25,0.75,0.5,0.25}},
{15,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{16,"relative probe rgba (0.5, 0.5) (0.125, 0.375, 0.25, 0.125)",SR_REL4,0,NULL,{0.5,0.5,0.125,0.375,0.25,0.125}},
{19,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{20,"relative probe rgba (0.5, 0.5) (0.125, 0.125, 0.125, 0.125)",SR_REL4,0,NULL,{0.5,0.5,0.125,0.125,0.125,0.125}},
{24,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{25,"relative probe rgba (0.5, 0.5) (0.25, 0.25, 0.25, 0.25)",SR_REL4,0,NULL,{0.5,0.5,0.25,0.25,0.25,0.25}},
{17,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{18,"relative probe rgba (0.5, 0.5) (0.85, 0.85, 0.85, 0.85)",SR_REL4,0,NULL,{0.5,0.5,0.85,0.85,0.85,0.85}},
{30,"uniform int tex 0",SR_U_INT,0,"tex",{0}},
{31,"texture shadow1D 0 (32)",SR_Z1,0,NULL,{0,32}},
{32,"texparameter 1D depth_mode luminance",SR_TEXPARAM,GL_TEXTURE_1D,NULL,{GL_DEPTH_TEXTURE_MODE,GL_LUMINANCE}},
{33,"texparameter 1D compare_func greater",SR_TEXPARAM,GL_TEXTURE_1D,NULL,{GL_TEXTURE_COMPARE_FUNC,GL_GREATER}},
{34,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{37,"relative probe rgba (0.0, 0.1) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.0,0.1,1.0,1.0,1.0,1.0}},
{38,"relative probe rgba (0.9, 1.0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.9,1.0,1.0,1.0,1.0,1.0}},
{41,"relative probe rgba (0.1, 0.0) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.1,0.0,0.0,0.0,0.0,1.0}},
{42,"relative probe rgba (1.0, 0.9) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{1.0,0.9,0.0,0.0,0.0,1.0}},
{30,"uniform int tex 0",SR_U_INT,0,"tex",{0}},
{31,"texture shadow1D 0 (32)",SR_Z1,0,NULL,{0,32}},
{32,"texparameter 1D depth_mode luminance",SR_TEXPARAM,GL_TEXTURE_1D,NULL,{GL_DEPTH_TEXTURE_MODE,GL_LUMINANCE}},
{33,"texparameter 1D compare_func always",SR_TEXPARAM,GL_TEXTURE_1D,NULL,{GL_TEXTURE_COMPARE_FUNC,GL_ALWAYS}},
{34,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{37,"relative probe rgba (0.0, 0.1) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.0,0.1,1.0,1.0,1.0,1.0}},
{38,"relative probe rgba (0.9, 1.0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.9,1.0,1.0,1.0,1.0,1.0}},
{41,"relative probe rgba (0.1, 0.0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.1,0.0,1.0,1.0,1.0,1.0}},
{42,"relative probe rgba (1.0, 0.9) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{1.0,0.9,1.0,1.0,1.0,1.0}},
{30,"uniform int tex 0",SR_U_INT,0,"tex",{0}},
{31,"texture shadow1D 0 (32)",SR_Z1,0,NULL,{0,32}},
{32,"texparameter 1D depth_mode luminance",SR_TEXPARAM,GL_TEXTURE_1D,NULL,{GL_DEPTH_TEXTURE_MODE,GL_LUMINANCE}},
{33,"texparameter 1D compare_func never",SR_TEXPARAM,GL_TEXTURE_1D,NULL,{GL_TEXTURE_COMPARE_FUNC,GL_NEVER}},
{34,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{37,"relative probe rgba (0.0, 0.1) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.0,0.1,0.0,0.0,0.0,1.0}},
{38,"relative probe rgba (0.9, 1.0) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.9,1.0,0.0,0.0,0.0,1.0}},
{41,"relative probe rgba (0.1, 0.0) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.1,0.0,0.0,0.0,0.0,1.0}},
{42,"relative probe rgba (1.0, 0.9) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{1.0,0.9,0.0,0.0,0.0,1.0}},
{30,"uniform int tex 0",SR_U_INT,0,"tex",{0}},
{31,"texture shadow1D 0 (32)",SR_Z1,0,NULL,{0,32}},
{32,"texparameter 1D depth_mode intensity",SR_TEXPARAM,GL_TEXTURE_1D,NULL,{GL_DEPTH_TEXTURE_MODE,GL_INTENSITY}},
{33,"texparameter 1D compare_func greater",SR_TEXPARAM,GL_TEXTURE_1D,NULL,{GL_TEXTURE_COMPARE_FUNC,GL_GREATER}},
{34,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{37,"relative probe rgba (0.0, 0.1) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.0,0.1,1.0,1.0,1.0,1.0}},
{38,"relative probe rgba (0.9, 1.0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.9,1.0,1.0,1.0,1.0,1.0}},
{41,"relative probe rgba (0.1, 0.0) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.1,0.0,0.0,0.0,0.0,0.0}},
{42,"relative probe rgba (1.0, 0.9) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{1.0,0.9,0.0,0.0,0.0,0.0}},
{30,"uniform int tex 0",SR_U_INT,0,"tex",{0}},
{31,"texture shadow1D 0 (32)",SR_Z1,0,NULL,{0,32}},
{32,"texparameter 1D depth_mode intensity",SR_TEXPARAM,GL_TEXTURE_1D,NULL,{GL_DEPTH_TEXTURE_MODE,GL_INTENSITY}},
{33,"texparameter 1D compare_func always",SR_TEXPARAM,GL_TEXTURE_1D,NULL,{GL_TEXTURE_COMPARE_FUNC,GL_ALWAYS}},
{34,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{37,"relative probe rgba (0.0, 0.1) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.0,0.1,1.0,1.0,1.0,1.0}},
{38,"relative probe rgba (0.9, 1.0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.9,1.0,1.0,1.0,1.0,1.0}},
{41,"relative probe rgba (0.1, 0.0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.1,0.0,1.0,1.0,1.0,1.0}},
{42,"relative probe rgba (1.0, 0.9) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{1.0,0.9,1.0,1.0,1.0,1.0}},
{30,"uniform int tex 0",SR_U_INT,0,"tex",{0}},
{31,"texture shadow1D 0 (32)",SR_Z1,0,NULL,{0,32}},
{32,"texparameter 1D depth_mode intensity",SR_TEXPARAM,GL_TEXTURE_1D,NULL,{GL_DEPTH_TEXTURE_MODE,GL_INTENSITY}},
{33,"texparameter 1D compare_func never",SR_TEXPARAM,GL_TEXTURE_1D,NULL,{GL_TEXTURE_COMPARE_FUNC,GL_NEVER}},
{34,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{37,"relative probe rgba (0.0, 0.1) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.0,0.1,0.0,0.0,0.0,0.0}},
{38,"relative probe rgba (0.9, 1.0) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.9,1.0,0.0,0.0,0.0,0.0}},
{41,"relative probe rgba (0.1, 0.0) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.1,0.0,0.0,0.0,0.0,0.0}},
{42,"relative probe rgba (1.0, 0.9) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{1.0,0.9,0.0,0.0,0.0,0.0}},
{30,"uniform int tex 0",SR_U_INT,0,"tex",{0}},
{31,"texture shadow1D 0 (32)",SR_Z1,0,NULL,{0,32}},
{32,"texparameter 1D depth_mode alpha",SR_TEXPARAM,GL_TEXTURE_1D,NULL,{GL_DEPTH_TEXTURE_MODE,GL_ALPHA}},
{33,"texparameter 1D compare_func greater",SR_TEXPARAM,GL_TEXTURE_1D,NULL,{GL_TEXTURE_COMPARE_FUNC,GL_GREATER}},
{34,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{37,"relative probe rgba (0.0, 0.1) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.0,0.1,0.0,0.0,0.0,1.0}},
{38,"relative probe rgba (0.9, 1.0) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.9,1.0,0.0,0.0,0.0,1.0}},
{41,"relative probe rgba (0.1, 0.0) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.1,0.0,0.0,0.0,0.0,0.0}},
{42,"relative probe rgba (1.0, 0.9) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{1.0,0.9,0.0,0.0,0.0,0.0}},
{30,"uniform int tex 0",SR_U_INT,0,"tex",{0}},
{31,"texture shadow1D 0 (32)",SR_Z1,0,NULL,{0,32}},
{32,"texparameter 1D depth_mode alpha",SR_TEXPARAM,GL_TEXTURE_1D,NULL,{GL_DEPTH_TEXTURE_MODE,GL_ALPHA}},
{33,"texparameter 1D compare_func always",SR_TEXPARAM,GL_TEXTURE_1D,NULL,{GL_TEXTURE_COMPARE_FUNC,GL_ALWAYS}},
{34,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{37,"relative probe rgba (0.0, 0.1) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.0,0.1,0.0,0.0,0.0,1.0}},
{38,"relative probe rgba (0.9, 1.0) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.9,1.0,0.0,0.0,0.0,1.0}},
{41,"relative probe rgba (0.1, 0.0) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.1,0.0,0.0,0.0,0.0,1.0}},
{42,"relative probe rgba (1.0, 0.9) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{1.0,0.9,0.0,0.0,0.0,1.0}},
{30,"uniform int tex 0",SR_U_INT,0,"tex",{0}},
{31,"texture shadow1D 0 (32)",SR_Z1,0,NULL,{0,32}},
{32,"texparameter 1D depth_mode alpha",SR_TEXPARAM,GL_TEXTURE_1D,NULL,{GL_DEPTH_TEXTURE_MODE,GL_ALPHA}},
{33,"texparameter 1D compare_func never",SR_TEXPARAM,GL_TEXTURE_1D,NULL,{GL_TEXTURE_COMPARE_FUNC,GL_NEVER}},
{34,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{37,"relative probe rgba (0.0, 0.1) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.0,0.1,0.0,0.0,0.0,0.0}},
{38,"relative probe rgba (0.9, 1.0) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.9,1.0,0.0,0.0,0.0,0.0}},
{41,"relative probe rgba (0.1, 0.0) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.1,0.0,0.0,0.0,0.0,0.0}},
{42,"relative probe rgba (1.0, 0.9) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{1.0,0.9,0.0,0.0,0.0,0.0}},
{24,"uniform int tex 0",SR_U_INT,0,"tex",{0}},
{25,"texture shadow1D 0 (32)",SR_Z1,0,NULL,{0,32}},
{26,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{27,"relative probe rgba (0.0, 0.1) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.0,0.1,1.0,1.0,1.0,1.0}},
{28,"relative probe rgba (0.0, 1.0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.0,1.0,1.0,1.0,1.0,1.0}},
{29,"relative probe rgba (0.9, 1.0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.9,1.0,1.0,1.0,1.0,1.0}},
{31,"relative probe rgba (0.1, 0.0) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.1,0.0,0.0,0.0,0.0,1.0}},
{32,"relative probe rgba (1.0, 0.0) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{1.0,0.0,0.0,0.0,0.0,1.0}},
{33,"relative probe rgba (1.0, 0.9) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{1.0,0.9,0.0,0.0,0.0,1.0}},
{24,"uniform int tex 0",SR_U_INT,0,"tex",{0}},
{25,"texture shadow1D 0 (32)",SR_Z1,0,NULL,{0,32}},
{26,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{27,"relative probe rgba (0.0, 0.1) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.0,0.1,1.0,1.0,1.0,1.0}},
{28,"relative probe rgba (0.0, 1.0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.0,1.0,1.0,1.0,1.0,1.0}},
{29,"relative probe rgba (0.9, 1.0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.9,1.0,1.0,1.0,1.0,1.0}},
{31,"relative probe rgba (0.1, 0.0) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.1,0.0,0.0,0.0,0.0,1.0}},
{32,"relative probe rgba (1.0, 0.0) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{1.0,0.0,0.0,0.0,0.0,1.0}},
{33,"relative probe rgba (1.0, 0.9) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{1.0,0.9,0.0,0.0,0.0,1.0}},
{23,"uniform int tex 0",SR_U_INT,0,"tex",{0}},
{24,"texture shadow1D 0 (32)",SR_Z1,0,NULL,{0,32}},
{25,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{26,"relative probe rgba (0.0, 0.1) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.0,0.1,1.0,1.0,1.0,1.0}},
{27,"relative probe rgba (0.0, 1.0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.0,1.0,1.0,1.0,1.0,1.0}},
{28,"relative probe rgba (0.9, 1.0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.9,1.0,1.0,1.0,1.0,1.0}},
{30,"relative probe rgba (0.1, 0.0) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.1,0.0,0.0,0.0,0.0,1.0}},
{31,"relative probe rgba (1.0, 0.0) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{1.0,0.0,0.0,0.0,0.0,1.0}},
{32,"relative probe rgba (1.0, 0.9) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{1.0,0.9,0.0,0.0,0.0,1.0}},
{23,"uniform int tex 0",SR_U_INT,0,"tex",{0}},
{24,"texture shadow1D 0 (32)",SR_Z1,0,NULL,{0,32}},
{25,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{26,"relative probe rgba (0.0, 0.1) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.0,0.1,1.0,1.0,1.0,1.0}},
{27,"relative probe rgba (0.0, 1.0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.0,1.0,1.0,1.0,1.0,1.0}},
{28,"relative probe rgba (0.9, 1.0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.9,1.0,1.0,1.0,1.0,1.0}},
{30,"relative probe rgba (0.1, 0.0) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.1,0.0,0.0,0.0,0.0,1.0}},
{31,"relative probe rgba (1.0, 0.0) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{1.0,0.0,0.0,0.0,0.0,1.0}},
{32,"relative probe rgba (1.0, 0.9) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{1.0,0.9,0.0,0.0,0.0,1.0}},
{30,"uniform int tex 0",SR_U_INT,0,"tex",{0}},
{31,"texture shadow2D 0 (32, 32)",SR_Z2,0,NULL,{0,32,32}},
{32,"texparameter 2D depth_mode luminance",SR_TEXPARAM,GL_TEXTURE_2D,NULL,{GL_DEPTH_TEXTURE_MODE,GL_LUMINANCE}},
{33,"texparameter 2D compare_func greater",SR_TEXPARAM,GL_TEXTURE_2D,NULL,{GL_TEXTURE_COMPARE_FUNC,GL_GREATER}},
{34,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{37,"relative probe rgba (0.0, 0.1) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.0,0.1,1.0,1.0,1.0,1.0}},
{38,"relative probe rgba (0.9, 1.0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.9,1.0,1.0,1.0,1.0,1.0}},
{41,"relative probe rgba (0.1, 0.0) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.1,0.0,0.0,0.0,0.0,1.0}},
{42,"relative probe rgba (1.0, 0.9) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{1.0,0.9,0.0,0.0,0.0,1.0}},
{30,"uniform int tex 0",SR_U_INT,0,"tex",{0}},
{31,"texture shadow2D 0 (32, 32)",SR_Z2,0,NULL,{0,32,32}},
{32,"texparameter 2D depth_mode luminance",SR_TEXPARAM,GL_TEXTURE_2D,NULL,{GL_DEPTH_TEXTURE_MODE,GL_LUMINANCE}},
{33,"texparameter 2D compare_func always",SR_TEXPARAM,GL_TEXTURE_2D,NULL,{GL_TEXTURE_COMPARE_FUNC,GL_ALWAYS}},
{34,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{37,"relative probe rgba (0.0, 0.1) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.0,0.1,1.0,1.0,1.0,1.0}},
{38,"relative probe rgba (0.9, 1.0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.9,1.0,1.0,1.0,1.0,1.0}},
{41,"relative probe rgba (0.1, 0.0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.1,0.0,1.0,1.0,1.0,1.0}},
{42,"relative probe rgba (1.0, 0.9) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{1.0,0.9,1.0,1.0,1.0,1.0}},
{30,"uniform int tex 0",SR_U_INT,0,"tex",{0}},
{31,"texture shadow2D 0 (32, 32)",SR_Z2,0,NULL,{0,32,32}},
{32,"texparameter 2D depth_mode luminance",SR_TEXPARAM,GL_TEXTURE_2D,NULL,{GL_DEPTH_TEXTURE_MODE,GL_LUMINANCE}},
{33,"texparameter 2D compare_func never",SR_TEXPARAM,GL_TEXTURE_2D,NULL,{GL_TEXTURE_COMPARE_FUNC,GL_NEVER}},
{34,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{37,"relative probe rgba (0.0, 0.1) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.0,0.1,0.0,0.0,0.0,1.0}},
{38,"relative probe rgba (0.9, 1.0) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.9,1.0,0.0,0.0,0.0,1.0}},
{41,"relative probe rgba (0.1, 0.0) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.1,0.0,0.0,0.0,0.0,1.0}},
{42,"relative probe rgba (1.0, 0.9) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{1.0,0.9,0.0,0.0,0.0,1.0}},
{30,"uniform int tex 0",SR_U_INT,0,"tex",{0}},
{31,"texture shadow2D 0 (32, 32)",SR_Z2,0,NULL,{0,32,32}},
{32,"texparameter 2D depth_mode intensity",SR_TEXPARAM,GL_TEXTURE_2D,NULL,{GL_DEPTH_TEXTURE_MODE,GL_INTENSITY}},
{33,"texparameter 2D compare_func greater",SR_TEXPARAM,GL_TEXTURE_2D,NULL,{GL_TEXTURE_COMPARE_FUNC,GL_GREATER}},
{34,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{37,"relative probe rgba (0.0, 0.1) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.0,0.1,1.0,1.0,1.0,1.0}},
{38,"relative probe rgba (0.9, 1.0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.9,1.0,1.0,1.0,1.0,1.0}},
{41,"relative probe rgba (0.1, 0.0) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.1,0.0,0.0,0.0,0.0,0.0}},
{42,"relative probe rgba (1.0, 0.9) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{1.0,0.9,0.0,0.0,0.0,0.0}},
{30,"uniform int tex 0",SR_U_INT,0,"tex",{0}},
{31,"texture shadow2D 0 (32, 32)",SR_Z2,0,NULL,{0,32,32}},
{32,"texparameter 2D depth_mode intensity",SR_TEXPARAM,GL_TEXTURE_2D,NULL,{GL_DEPTH_TEXTURE_MODE,GL_INTENSITY}},
{33,"texparameter 2D compare_func always",SR_TEXPARAM,GL_TEXTURE_2D,NULL,{GL_TEXTURE_COMPARE_FUNC,GL_ALWAYS}},
{34,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{37,"relative probe rgba (0.0, 0.1) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.0,0.1,1.0,1.0,1.0,1.0}},
{38,"relative probe rgba (0.9, 1.0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.9,1.0,1.0,1.0,1.0,1.0}},
{41,"relative probe rgba (0.1, 0.0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.1,0.0,1.0,1.0,1.0,1.0}},
{42,"relative probe rgba (1.0, 0.9) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{1.0,0.9,1.0,1.0,1.0,1.0}},
{30,"uniform int tex 0",SR_U_INT,0,"tex",{0}},
{31,"texture shadow2D 0 (32, 32)",SR_Z2,0,NULL,{0,32,32}},
{32,"texparameter 2D depth_mode intensity",SR_TEXPARAM,GL_TEXTURE_2D,NULL,{GL_DEPTH_TEXTURE_MODE,GL_INTENSITY}},
{33,"texparameter 2D compare_func never",SR_TEXPARAM,GL_TEXTURE_2D,NULL,{GL_TEXTURE_COMPARE_FUNC,GL_NEVER}},
{34,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{37,"relative probe rgba (0.0, 0.1) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.0,0.1,0.0,0.0,0.0,0.0}},
{38,"relative probe rgba (0.9, 1.0) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.9,1.0,0.0,0.0,0.0,0.0}},
{41,"relative probe rgba (0.1, 0.0) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.1,0.0,0.0,0.0,0.0,0.0}},
{42,"relative probe rgba (1.0, 0.9) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{1.0,0.9,0.0,0.0,0.0,0.0}},
{30,"uniform int tex 0",SR_U_INT,0,"tex",{0}},
{31,"texture shadow2D 0 (32, 32)",SR_Z2,0,NULL,{0,32,32}},
{32,"texparameter 2D depth_mode alpha",SR_TEXPARAM,GL_TEXTURE_2D,NULL,{GL_DEPTH_TEXTURE_MODE,GL_ALPHA}},
{33,"texparameter 2D compare_func greater",SR_TEXPARAM,GL_TEXTURE_2D,NULL,{GL_TEXTURE_COMPARE_FUNC,GL_GREATER}},
{34,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{37,"relative probe rgba (0.0, 0.1) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.0,0.1,0.0,0.0,0.0,1.0}},
{38,"relative probe rgba (0.9, 1.0) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.9,1.0,0.0,0.0,0.0,1.0}},
{41,"relative probe rgba (0.1, 0.0) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.1,0.0,0.0,0.0,0.0,0.0}},
{42,"relative probe rgba (1.0, 0.9) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{1.0,0.9,0.0,0.0,0.0,0.0}},
{30,"uniform int tex 0",SR_U_INT,0,"tex",{0}},
{31,"texture shadow2D 0 (32, 32)",SR_Z2,0,NULL,{0,32,32}},
{32,"texparameter 2D depth_mode alpha",SR_TEXPARAM,GL_TEXTURE_2D,NULL,{GL_DEPTH_TEXTURE_MODE,GL_ALPHA}},
{33,"texparameter 2D compare_func always",SR_TEXPARAM,GL_TEXTURE_2D,NULL,{GL_TEXTURE_COMPARE_FUNC,GL_ALWAYS}},
{34,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{37,"relative probe rgba (0.0, 0.1) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.0,0.1,0.0,0.0,0.0,1.0}},
{38,"relative probe rgba (0.9, 1.0) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.9,1.0,0.0,0.0,0.0,1.0}},
{41,"relative probe rgba (0.1, 0.0) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.1,0.0,0.0,0.0,0.0,1.0}},
{42,"relative probe rgba (1.0, 0.9) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{1.0,0.9,0.0,0.0,0.0,1.0}},
{30,"uniform int tex 0",SR_U_INT,0,"tex",{0}},
{31,"texture shadow2D 0 (32, 32)",SR_Z2,0,NULL,{0,32,32}},
{32,"texparameter 2D depth_mode alpha",SR_TEXPARAM,GL_TEXTURE_2D,NULL,{GL_DEPTH_TEXTURE_MODE,GL_ALPHA}},
{33,"texparameter 2D compare_func never",SR_TEXPARAM,GL_TEXTURE_2D,NULL,{GL_TEXTURE_COMPARE_FUNC,GL_NEVER}},
{34,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{37,"relative probe rgba (0.0, 0.1) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.0,0.1,0.0,0.0,0.0,0.0}},
{38,"relative probe rgba (0.9, 1.0) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.9,1.0,0.0,0.0,0.0,0.0}},
{41,"relative probe rgba (0.1, 0.0) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.1,0.0,0.0,0.0,0.0,0.0}},
{42,"relative probe rgba (1.0, 0.9) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{1.0,0.9,0.0,0.0,0.0,0.0}},
{24,"uniform int tex 0",SR_U_INT,0,"tex",{0}},
{25,"texture shadow2D 0 (32, 32)",SR_Z2,0,NULL,{0,32,32}},
{26,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{27,"relative probe rgba (0.0, 0.1) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.0,0.1,1.0,1.0,1.0,1.0}},
{28,"relative probe rgba (0.0, 1.0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.0,1.0,1.0,1.0,1.0,1.0}},
{29,"relative probe rgba (0.9, 1.0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.9,1.0,1.0,1.0,1.0,1.0}},
{31,"relative probe rgba (0.1, 0.0) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.1,0.0,0.0,0.0,0.0,1.0}},
{32,"relative probe rgba (1.0, 0.0) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{1.0,0.0,0.0,0.0,0.0,1.0}},
{33,"relative probe rgba (1.0, 0.9) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{1.0,0.9,0.0,0.0,0.0,1.0}},
{33,"uniform int tex 0",SR_U_INT,0,"tex",{0}},
{34,"texture shadow2D 0 (32, 32)",SR_Z2,0,NULL,{0,32,32}},
{35,"texparameter 2D compare_func equal",SR_TEXPARAM,GL_TEXTURE_2D,NULL,{GL_TEXTURE_COMPARE_FUNC,GL_EQUAL}},
{37,"uniform vec3 texcoords 0 0 -1",SR_U_VEC3,0,"texcoords",{0,0,-1}},
{38,"draw rect -1 -1 1 2",SR_QUAD,0,NULL,{-1,-1,1,2}},
{40,"uniform vec3 texcoords 1 0 2",SR_U_VEC3,0,"texcoords",{1,0,2}},
{41,"draw rect 0 -1 1 2",SR_QUAD,0,NULL,{0,-1,1,2}},
{43,"relative probe rgba (0.25, 0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.25,0,1.0,1.0,1.0,1.0}},
{44,"relative probe rgba (0.75, 0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.75,0,1.0,1.0,1.0,1.0}},
{24,"uniform int tex 0",SR_U_INT,0,"tex",{0}},
{25,"texture shadow2D 0 (32, 32)",SR_Z2,0,NULL,{0,32,32}},
{26,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{27,"relative probe rgba (0.0, 0.1) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.0,0.1,1.0,1.0,1.0,1.0}},
{28,"relative probe rgba (0.0, 1.0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.0,1.0,1.0,1.0,1.0,1.0}},
{29,"relative probe rgba (0.9, 1.0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.9,1.0,1.0,1.0,1.0,1.0}},
{31,"relative probe rgba (0.1, 0.0) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.1,0.0,0.0,0.0,0.0,1.0}},
{32,"relative probe rgba (1.0, 0.0) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{1.0,0.0,0.0,0.0,0.0,1.0}},
{33,"relative probe rgba (1.0, 0.9) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{1.0,0.9,0.0,0.0,0.0,1.0}},
{23,"uniform int tex 0",SR_U_INT,0,"tex",{0}},
{24,"texture shadow2D 0 (32, 32)",SR_Z2,0,NULL,{0,32,32}},
{25,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{26,"relative probe rgba (0.0, 0.1) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.0,0.1,1.0,1.0,1.0,1.0}},
{27,"relative probe rgba (0.0, 1.0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.0,1.0,1.0,1.0,1.0,1.0}},
{28,"relative probe rgba (0.9, 1.0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.9,1.0,1.0,1.0,1.0,1.0}},
{30,"relative probe rgba (0.1, 0.0) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.1,0.0,0.0,0.0,0.0,1.0}},
{31,"relative probe rgba (1.0, 0.0) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{1.0,0.0,0.0,0.0,0.0,1.0}},
{32,"relative probe rgba (1.0, 0.9) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{1.0,0.9,0.0,0.0,0.0,1.0}},
{23,"uniform int tex 0",SR_U_INT,0,"tex",{0}},
{24,"texture shadow2D 0 (32, 32)",SR_Z2,0,NULL,{0,32,32}},
{25,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{26,"relative probe rgba (0.0, 0.1) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.0,0.1,1.0,1.0,1.0,1.0}},
{27,"relative probe rgba (0.0, 1.0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.0,1.0,1.0,1.0,1.0,1.0}},
{28,"relative probe rgba (0.9, 1.0) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.9,1.0,1.0,1.0,1.0,1.0}},
{30,"relative probe rgba (0.1, 0.0) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.1,0.0,0.0,0.0,0.0,1.0}},
{31,"relative probe rgba (1.0, 0.0) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{1.0,0.0,0.0,0.0,0.0,1.0}},
{32,"relative probe rgba (1.0, 0.9) (0.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{1.0,0.9,0.0,0.0,0.0,1.0}},
{31,"uniform int tex[0] 0",SR_U_INT,0,"tex[0]",{0}},
{32,"uniform int tex[1] 1",SR_U_INT,0,"tex[1]",{1}},
{33,"texture checkerboard 0 0 (8, 8) (1.0, 0.0, 0.0, 0.0) (0.0, 0.0, 1.0, 0.0)",SR_CHECKER,0,NULL,{0,0,8,8,1.0,0.0,0.0,0.0,0.0,0.0,1.0,0.0}},
{34,"texture checkerboard 1 0 (8, 8) (0.0, 1.0, 1.0, 0.0) (1.0, 1.0, 0.0, 0.0)",SR_CHECKER,0,NULL,{1,0,8,8,0.0,1.0,1.0,0.0,1.0,1.0,0.0,0.0}},
{35,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{36,"relative probe rgb (0.25, 0.25) (1.0, 0.0, 0.0)",SR_REL3,0,NULL,{0.25,0.25,1.0,0.0,0.0}},
{37,"relative probe rgb (0.25, 0.75) (0.0, 0.0, 1.0)",SR_REL3,0,NULL,{0.25,0.75,0.0,0.0,1.0}},
{38,"relative probe rgb (0.75, 0.25) (1.0, 1.0, 0.0)",SR_REL3,0,NULL,{0.75,0.25,1.0,1.0,0.0}},
{39,"relative probe rgb (0.75, 0.75) (0.0, 1.0, 1.0)",SR_REL3,0,NULL,{0.75,0.75,0.0,1.0,1.0}},
{36,"uniform int s.s.tex 1",SR_U_INT,0,"s.s.tex",{1}},
{37,"uniform float s.s.f 1.0",SR_U_FLOAT,0,"s.s.f",{1.0}},
{38,"uniform float s.f 1.0",SR_U_FLOAT,0,"s.f",{1.0}},
{39,"texture rgbw 1 (8, 8)",SR_RGB2,0,NULL,{1,8,8}},
{40,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{41,"relative probe rgb (0.25, 0.25) (1.0, 0.0, 0.0)",SR_REL3,0,NULL,{0.25,0.25,1.0,0.0,0.0}},
{42,"relative probe rgb (0.75, 0.25) (0.0, 1.0, 0.0)",SR_REL3,0,NULL,{0.75,0.25,0.0,1.0,0.0}},
{43,"relative probe rgb (0.25, 0.75) (0.0, 0.0, 1.0)",SR_REL3,0,NULL,{0.25,0.75,0.0,0.0,1.0}},
{44,"relative probe rgb (0.75, 0.75) (1.0, 1.0, 1.0)",SR_REL3,0,NULL,{0.75,0.75,1.0,1.0,1.0}},
{32,"uniform int s.tex[0] 1",SR_U_INT,0,"s.tex[0]",{1}},
{33,"uniform float s.f 1.0",SR_U_FLOAT,0,"s.f",{1.0}},
{34,"texture rgbw 1 (8, 8)",SR_RGB2,0,NULL,{1,8,8}},
{35,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{36,"relative probe rgb (0.25, 0.25) (1.0, 0.0, 0.0)",SR_REL3,0,NULL,{0.25,0.25,1.0,0.0,0.0}},
{37,"relative probe rgb (0.75, 0.25) (0.0, 1.0, 0.0)",SR_REL3,0,NULL,{0.75,0.25,0.0,1.0,0.0}},
{38,"relative probe rgb (0.25, 0.75) (0.0, 0.0, 1.0)",SR_REL3,0,NULL,{0.25,0.75,0.0,0.0,1.0}},
{39,"relative probe rgb (0.75, 0.75) (1.0, 1.0, 1.0)",SR_REL3,0,NULL,{0.75,0.75,1.0,1.0,1.0}},
{32,"uniform int s.tex 1",SR_U_INT,0,"s.tex",{1}},
{33,"uniform float s.f 1.0",SR_U_FLOAT,0,"s.f",{1.0}},
{34,"texture rgbw 1 (8, 8)",SR_RGB2,0,NULL,{1,8,8}},
{35,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{36,"relative probe rgb (0.25, 0.25) (1.0, 0.0, 0.0)",SR_REL3,0,NULL,{0.25,0.25,1.0,0.0,0.0}},
{37,"relative probe rgb (0.75, 0.25) (0.0, 1.0, 0.0)",SR_REL3,0,NULL,{0.75,0.25,0.0,1.0,0.0}},
{38,"relative probe rgb (0.25, 0.75) (0.0, 0.0, 1.0)",SR_REL3,0,NULL,{0.25,0.75,0.0,0.0,1.0}},
{39,"relative probe rgb (0.75, 0.75) (1.0, 1.0, 1.0)",SR_REL3,0,NULL,{0.75,0.75,1.0,1.0,1.0}},
{29,"uniform int tex 1",SR_U_INT,0,"tex",{1}},
{30,"uniform float f 1.0",SR_U_FLOAT,0,"f",{1.0}},
{31,"texture rgbw 1 (8, 8)",SR_RGB2,0,NULL,{1,8,8}},
{32,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{33,"relative probe rgb (0.25, 0.25) (1.0, 0.0, 0.0)",SR_REL3,0,NULL,{0.25,0.25,1.0,0.0,0.0}},
{34,"relative probe rgb (0.75, 0.25) (0.0, 1.0, 0.0)",SR_REL3,0,NULL,{0.75,0.25,0.0,1.0,0.0}},
{35,"relative probe rgb (0.25, 0.75) (0.0, 0.0, 1.0)",SR_REL3,0,NULL,{0.25,0.75,0.0,0.0,1.0}},
{36,"relative probe rgb (0.75, 0.75) (1.0, 1.0, 1.0)",SR_REL3,0,NULL,{0.75,0.75,1.0,1.0,1.0}},
{32,"uniform int s.tex 1",SR_U_INT,0,"s.tex",{1}},
{33,"uniform float s.f 1.0",SR_U_FLOAT,0,"s.f",{1.0}},
{34,"texture rgbw 1 (8, 8)",SR_RGB2,0,NULL,{1,8,8}},
{35,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{36,"relative probe rgb (0.25, 0.25) (1.0, 0.0, 0.0)",SR_REL3,0,NULL,{0.25,0.25,1.0,0.0,0.0}},
{37,"relative probe rgb (0.75, 0.25) (0.0, 1.0, 0.0)",SR_REL3,0,NULL,{0.75,0.25,0.0,1.0,0.0}},
{38,"relative probe rgb (0.25, 0.75) (0.0, 0.0, 1.0)",SR_REL3,0,NULL,{0.25,0.75,0.0,0.0,1.0}},
{39,"relative probe rgb (0.75, 0.75) (1.0, 1.0, 1.0)",SR_REL3,0,NULL,{0.75,0.75,1.0,1.0,1.0}},
{29,"uniform int tex 1",SR_U_INT,0,"tex",{1}},
{30,"uniform float f 1.0",SR_U_FLOAT,0,"f",{1.0}},
{31,"texture rgbw 1 (8, 8)",SR_RGB2,0,NULL,{1,8,8}},
{32,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{33,"relative probe rgb (0.25, 0.25) (1.0, 0.0, 0.0)",SR_REL3,0,NULL,{0.25,0.25,1.0,0.0,0.0}},
{34,"relative probe rgb (0.75, 0.25) (0.0, 1.0, 0.0)",SR_REL3,0,NULL,{0.75,0.25,0.0,1.0,0.0}},
{35,"relative probe rgb (0.25, 0.75) (0.0, 0.0, 1.0)",SR_REL3,0,NULL,{0.25,0.75,0.0,0.0,1.0}},
{36,"relative probe rgb (0.75, 0.75) (1.0, 1.0, 1.0)",SR_REL3,0,NULL,{0.75,0.75,1.0,1.0,1.0}},
{14,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{15,"relative probe rgba (0.5, 0.5) (0.5, 0.5, 0.5, 0.5)",SR_REL4,0,NULL,{0.5,0.5,0.5,0.5,0.5,0.5}},
{21,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{22,"relative probe rgba (0.5, 0.5) (0.25, 0.5, 0.75, 1.0)",SR_REL4,0,NULL,{0.5,0.5,0.25,0.5,0.75,1.0}},
{22,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{23,"relative probe rgba (0.5, 0.5) (0.25, 0.5, 0.75, 1.0)",SR_REL4,0,NULL,{0.5,0.5,0.25,0.5,0.75,1.0}},
{27,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{28,"relative probe rgba (0.5, 0.5) (0.25, 0.5, 0.75, 1.0)",SR_REL4,0,NULL,{0.5,0.5,0.25,0.5,0.75,1.0}},
{22,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{23,"relative probe rgba (0.5, 0.5) (0.225, 0.35, 0.475, 0.6)",SR_REL4,0,NULL,{0.5,0.5,0.225,0.35,0.475,0.6}},
{13,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{14,"relative probe rgba (0.5, 0.5) (0.25, 0.5, 0.5, 0.0)",SR_REL4,0,NULL,{0.5,0.5,0.25,0.5,0.5,0.0}},
{13,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{14,"relative probe rgba (0.5, 0.5) (0.25, 0.5, 0.5, 0.0)",SR_REL4,0,NULL,{0.5,0.5,0.25,0.5,0.5,0.0}},
{13,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{14,"relative probe rgba (0.5, 0.5) (0.25, 0.5, 0.5, 0.0)",SR_REL4,0,NULL,{0.5,0.5,0.25,0.5,0.5,0.0}},
{15,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{16,"relative probe rgba (0.5, 0.5) (0.8, 0.1, 0.2, 0.5)",SR_REL4,0,NULL,{0.5,0.5,0.8,0.1,0.2,0.5}},
{14,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{15,"relative probe rgba (0.5, 0.5) (0.3, 0.3, 0.4, 0.1)",SR_REL4,0,NULL,{0.5,0.5,0.3,0.3,0.4,0.1}},
{13,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{14,"relative probe rgba (0.5, 0.5) (0.75, 0.5, 1.0, 0.25)",SR_REL4,0,NULL,{0.5,0.5,0.75,0.5,1.0,0.25}},
{13,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{14,"relative probe rgba (0.5, 0.5) (0.75, 0.5, 1.0, 0.25)",SR_REL4,0,NULL,{0.5,0.5,0.75,0.5,1.0,0.25}},
{13,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{14,"relative probe rgba (0.5, 0.5) (0.75, 0.5, 1.0, 0.25)",SR_REL4,0,NULL,{0.5,0.5,0.75,0.5,1.0,0.25}},
{12,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{13,"relative probe rgba (0.5, 0.5) (0.25, 0.75, 1.0, 0.5)",SR_REL4,0,NULL,{0.5,0.5,0.25,0.75,1.0,0.5}},
{51,"uniform int readIndex 1",SR_U_INT,0,"readIndex",{1}},
{52,"uniform int writeIndex 1",SR_U_INT,0,"writeIndex",{1}},
{53,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{54,"relative probe rgb (0.0, 0.0) (1.0, 0.5, 1.0, 1.0)",SR_REL3,0,NULL,{0.0,0.0,1.0,0.5,1.0,1.0}},
{55,"relative probe rgb (1.0, 0.0) (1.0, 0.5, 1.0, 1.0)",SR_REL3,0,NULL,{1.0,0.0,1.0,0.5,1.0,1.0}},
{56,"relative probe rgb (0.0, 1.0) (1.0, 0.5, 1.0, 1.0)",SR_REL3,0,NULL,{0.0,1.0,1.0,0.5,1.0,1.0}},
{57,"relative probe rgb (1.0, 1.0) (1.0, 0.5, 1.0, 1.0)",SR_REL3,0,NULL,{1.0,1.0,1.0,0.5,1.0,1.0}},
{13,"uniform int tex1d 0",SR_U_INT,0,"tex1d",{0}},
{14,"texture rgbw 1D 0",SR_RGB1,0,NULL,{0}},
{15,"draw rect tex -1 -1 2 2 0 0 1 1",SR_QUADT,0,NULL,{-1,-1,2,2,0,0,1,1}},
{16,"relative probe rgba (0.125, 0.5) (1.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.125,0.5,1.0,0.0,0.0,1.0}},
{17,"relative probe rgba (0.375, 0.5) (0.0, 1.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.375,0.5,0.0,1.0,0.0,1.0}},
{18,"relative probe rgba (0.625, 0.5) (0.0, 0.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.625,0.5,0.0,0.0,1.0,1.0}},
{19,"relative probe rgba (0.875, 0.5) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.875,0.5,1.0,1.0,1.0,1.0}},
{14,"texture rgbw 0 (8, 8)",SR_RGB2,0,NULL,{0,8,8}},
{15,"uniform int tex2d 0",SR_U_INT,0,"tex2d",{0}},
{16,"draw rect tex -1 -1 2 2 0 0 1 1",SR_QUADT,0,NULL,{-1,-1,2,2,0,0,1,1}},
{17,"relative probe rgba (0.5, 0.5) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.5,1.0,1.0,1.0,1.0}},
{16,"uniform int tex3d 0",SR_U_INT,0,"tex3d",{0}},
{17,"texture rgbw 3D 0",SR_RGB3,0,NULL,{0}},
{18,"draw rect tex -1 -1 2 2 0 0 1 1",SR_QUADT,0,NULL,{-1,-1,2,2,0,0,1,1}},
{19,"relative probe rgba (0.25, 0.25) (1.0, 1.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.25,0.25,1.0,1.0,0.0,1.0}},
{20,"relative probe rgba (0.75, 0.25) (1.0, 0.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.75,0.25,1.0,0.0,1.0,1.0}},
{21,"relative probe rgba (0.25, 0.75) (1.0, 1.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.25,0.75,1.0,1.0,0.0,1.0}},
{22,"relative probe rgba (0.75, 0.75) (1.0, 0.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.75,0.75,1.0,0.0,1.0,1.0}},
{13,"uniform int tex3d 0",SR_U_INT,0,"tex3d",{0}},
{14,"texture rgbw 3D 0",SR_RGB3,0,NULL,{0}},
{15,"draw rect tex -1 -1 2 2 0 0 1 1",SR_QUADT,0,NULL,{-1,-1,2,2,0,0,1,1}},
{16,"relative probe rgba (0.25, 0.25) (1.0, 0.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.25,0.25,1.0,0.0,0.0,1.0}},
{17,"relative probe rgba (0.75, 0.25) (0.0, 1.0, 0.0, 1.0)",SR_REL4,0,NULL,{0.75,0.25,0.0,1.0,0.0,1.0}},
{18,"relative probe rgba (0.25, 0.75) (0.0, 0.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.25,0.75,0.0,0.0,1.0,1.0}},
{19,"relative probe rgba (0.75, 0.75) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.75,0.75,1.0,1.0,1.0,1.0}},
{13,"uniform mat4 uniformMat4 1.0 0.1 0.2 0.3  0.0 1.0 0.0 0.4  0.0 1.0 1.0 0.5  0.6 0.7 0.8 1.0",SR_U_MAT4,0,"uniformMat4",{1.0,0.1,0.2,0.3,0.0,1.0,0.0,0.4,0.0,1.0,1.0,0.5,0.6,0.7,0.8,1.0}},
{14,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{15,"relative probe rgba (0.5, 0.5) (0.6, 0.7, 0.8, 1.0)",SR_REL4,0,NULL,{0.5,0.5,0.6,0.7,0.8,1.0}},
{27,"uniform float zero0 0",SR_U_FLOAT,0,"zero0",{0}},
{28,"uniform float zero1 0",SR_U_FLOAT,0,"zero1",{0}},
{29,"uniform float zero2 0",SR_U_FLOAT,0,"zero2",{0}},
{30,"uniform float u 0.3",SR_U_FLOAT,0,"u",{0.3}},
{32,"clear color 0.0 0.0 0.0 0.0",SR_CLEARCOLOR,0,NULL,{0.0,0.0,0.0,0.0}},
{33,"clear",SR_CLEAR,0,NULL,{0}},
{34,"ortho",SR_ORTHOWINDOW,0,NULL,{0}},
{36,"draw rect 0 0 50 50",SR_QUAD,0,NULL,{0,0,50,50}},
{38,"uniform float u 0.4",SR_U_FLOAT,0,"u",{0.4}},
{39,"draw rect 50 0 50 50",SR_QUAD,0,NULL,{50,0,50,50}},
{41,"uniform float u 0.5",SR_U_FLOAT,0,"u",{0.5}},
{42,"draw rect 100 0 50 50",SR_QUAD,0,NULL,{100,0,50,50}},
{44,"uniform float u 0.6",SR_U_FLOAT,0,"u",{0.6}},
{45,"draw rect 150 0 50 50",SR_QUAD,0,NULL,{150,0,50,50}},
{47,"probe rgb 25 25 0.3 0.3 0.3",SR_PIX3,0,NULL,{25,25,0.3,0.3,0.3}},
{48,"probe rgb 75 25 0.4 0.4 0.4",SR_PIX3,0,NULL,{75,25,0.4,0.4,0.4}},
{49,"probe rgb 125 25 0.5 0.5 0.5",SR_PIX3,0,NULL,{125,25,0.5,0.5,0.5}},
{50,"probe rgb 175 25 0.6 0.6 0.6",SR_PIX3,0,NULL,{175,25,0.6,0.6,0.6}},
{35,"clear color 0.5 0.5 0.5 0.5",SR_CLEARCOLOR,0,NULL,{0.5,0.5,0.5,0.5}},
{36,"clear",SR_CLEAR,0,NULL,{0}},
{37,"ortho",SR_ORTHOWINDOW,0,NULL,{0}},
{39,"uniform int index 0",SR_U_INT,0,"index",{0}},
{40,"uniform float expect 1",SR_U_FLOAT,0,"expect",{1}},
{41,"draw rect 5 5 10 10",SR_QUAD,0,NULL,{5,5,10,10}},
{42,"probe rgb 10 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{10,10,0.0,1.0,0.0}},
{44,"uniform int index 1",SR_U_INT,0,"index",{1}},
{45,"uniform float expect 2",SR_U_FLOAT,0,"expect",{2}},
{46,"draw rect 30 5 10 10",SR_QUAD,0,NULL,{30,5,10,10}},
{47,"probe rgb 35 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{35,10,0.0,1.0,0.0}},
{49,"uniform int index 2",SR_U_INT,0,"index",{2}},
{50,"uniform float expect 3",SR_U_FLOAT,0,"expect",{3}},
{51,"draw rect 55 5 10 10",SR_QUAD,0,NULL,{55,5,10,10}},
{52,"probe rgb 60 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{60,10,0.0,1.0,0.0}},
{54,"uniform int index 3",SR_U_INT,0,"index",{3}},
{55,"uniform float expect 4",SR_U_FLOAT,0,"expect",{4}},
{56,"draw rect 80 5 10 10",SR_QUAD,0,NULL,{80,5,10,10}},
{57,"probe rgb 85 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{85,10,0.0,1.0,0.0}},
{59,"uniform int index 4",SR_U_INT,0,"index",{4}},
{60,"uniform float expect 5",SR_U_FLOAT,0,"expect",{5}},
{61,"draw rect 105 5 10 10",SR_QUAD,0,NULL,{105,5,10,10}},
{62,"probe rgb 110 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{110,10,0.0,1.0,0.0}},
{64,"uniform int index 5",SR_U_INT,0,"index",{5}},
{65,"uniform float expect 6",SR_U_FLOAT,0,"expect",{6}},
{66,"draw rect 130 5 10 10",SR_QUAD,0,NULL,{130,5,10,10}},
{67,"probe rgb 135 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{135,10,0.0,1.0,0.0}},
{69,"uniform int index 6",SR_U_INT,0,"index",{6}},
{70,"uniform float expect 7",SR_U_FLOAT,0,"expect",{7}},
{71,"draw rect 155 5 10 10",SR_QUAD,0,NULL,{155,5,10,10}},
{72,"probe rgb 160 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{160,10,0.0,1.0,0.0}},
{74,"uniform int index 7",SR_U_INT,0,"index",{7}},
{75,"uniform float expect 8",SR_U_FLOAT,0,"expect",{8}},
{76,"draw rect 180 5 10 10",SR_QUAD,0,NULL,{180,5,10,10}},
{77,"probe rgb 185 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{185,10,0.0,1.0,0.0}},
{35,"clear color 0.5 0.5 0.5 0.5",SR_CLEARCOLOR,0,NULL,{0.5,0.5,0.5,0.5}},
{36,"clear",SR_CLEAR,0,NULL,{0}},
{37,"ortho",SR_ORTHOWINDOW,0,NULL,{0}},
{39,"uniform int index 0",SR_U_INT,0,"index",{0}},
{40,"uniform vec2 expect 1 1.1",SR_U_VEC2,0,"expect",{1,1.1}},
{41,"draw rect 5 5 10 10",SR_QUAD,0,NULL,{5,5,10,10}},
{42,"probe rgb 10 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{10,10,0.0,1.0,0.0}},
{44,"uniform int index 1",SR_U_INT,0,"index",{1}},
{45,"uniform vec2 expect 2 2.1",SR_U_VEC2,0,"expect",{2,2.1}},
{46,"draw rect 30 5 10 10",SR_QUAD,0,NULL,{30,5,10,10}},
{47,"probe rgb 35 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{35,10,0.0,1.0,0.0}},
{49,"uniform int index 2",SR_U_INT,0,"index",{2}},
{50,"uniform vec2 expect 3 3.1",SR_U_VEC2,0,"expect",{3,3.1}},
{51,"draw rect 55 5 10 10",SR_QUAD,0,NULL,{55,5,10,10}},
{52,"probe rgb 60 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{60,10,0.0,1.0,0.0}},
{54,"uniform int index 3",SR_U_INT,0,"index",{3}},
{55,"uniform vec2 expect 4 4.1",SR_U_VEC2,0,"expect",{4,4.1}},
{56,"draw rect 80 5 10 10",SR_QUAD,0,NULL,{80,5,10,10}},
{57,"probe rgb 85 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{85,10,0.0,1.0,0.0}},
{59,"uniform int index 4",SR_U_INT,0,"index",{4}},
{60,"uniform vec2 expect 5 5.1",SR_U_VEC2,0,"expect",{5,5.1}},
{61,"draw rect 105 5 10 10",SR_QUAD,0,NULL,{105,5,10,10}},
{62,"probe rgb 110 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{110,10,0.0,1.0,0.0}},
{64,"uniform int index 5",SR_U_INT,0,"index",{5}},
{65,"uniform vec2 expect 6 6.1",SR_U_VEC2,0,"expect",{6,6.1}},
{66,"draw rect 130 5 10 10",SR_QUAD,0,NULL,{130,5,10,10}},
{67,"probe rgb 135 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{135,10,0.0,1.0,0.0}},
{69,"uniform int index 6",SR_U_INT,0,"index",{6}},
{70,"uniform vec2 expect 7 7.1",SR_U_VEC2,0,"expect",{7,7.1}},
{71,"draw rect 155 5 10 10",SR_QUAD,0,NULL,{155,5,10,10}},
{72,"probe rgb 160 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{160,10,0.0,1.0,0.0}},
{74,"uniform int index 7",SR_U_INT,0,"index",{7}},
{75,"uniform vec2 expect 8 8.1",SR_U_VEC2,0,"expect",{8,8.1}},
{76,"draw rect 180 5 10 10",SR_QUAD,0,NULL,{180,5,10,10}},
{77,"probe rgb 185 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{185,10,0.0,1.0,0.0}},
{35,"clear color 0.5 0.5 0.5 0.5",SR_CLEARCOLOR,0,NULL,{0.5,0.5,0.5,0.5}},
{36,"clear",SR_CLEAR,0,NULL,{0}},
{37,"ortho",SR_ORTHOWINDOW,0,NULL,{0}},
{39,"uniform int index 0",SR_U_INT,0,"index",{0}},
{40,"uniform vec3 expect 1 1.1 1.2",SR_U_VEC3,0,"expect",{1,1.1,1.2}},
{41,"draw rect 5 5 10 10",SR_QUAD,0,NULL,{5,5,10,10}},
{42,"probe rgb 10 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{10,10,0.0,1.0,0.0}},
{44,"uniform int index 1",SR_U_INT,0,"index",{1}},
{45,"uniform vec3 expect 2 2.1 2.2",SR_U_VEC3,0,"expect",{2,2.1,2.2}},
{46,"draw rect 30 5 10 10",SR_QUAD,0,NULL,{30,5,10,10}},
{47,"probe rgb 35 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{35,10,0.0,1.0,0.0}},
{49,"uniform int index 2",SR_U_INT,0,"index",{2}},
{50,"uniform vec3 expect 3 3.1 3.2",SR_U_VEC3,0,"expect",{3,3.1,3.2}},
{51,"draw rect 55 5 10 10",SR_QUAD,0,NULL,{55,5,10,10}},
{52,"probe rgb 60 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{60,10,0.0,1.0,0.0}},
{54,"uniform int index 3",SR_U_INT,0,"index",{3}},
{55,"uniform vec3 expect 4 4.1 4.2",SR_U_VEC3,0,"expect",{4,4.1,4.2}},
{56,"draw rect 80 5 10 10",SR_QUAD,0,NULL,{80,5,10,10}},
{57,"probe rgb 85 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{85,10,0.0,1.0,0.0}},
{59,"uniform int index 4",SR_U_INT,0,"index",{4}},
{60,"uniform vec3 expect 5 5.1 5.2",SR_U_VEC3,0,"expect",{5,5.1,5.2}},
{61,"draw rect 105 5 10 10",SR_QUAD,0,NULL,{105,5,10,10}},
{62,"probe rgb 110 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{110,10,0.0,1.0,0.0}},
{64,"uniform int index 5",SR_U_INT,0,"index",{5}},
{65,"uniform vec3 expect 6 6.1 6.2",SR_U_VEC3,0,"expect",{6,6.1,6.2}},
{66,"draw rect 130 5 10 10",SR_QUAD,0,NULL,{130,5,10,10}},
{67,"probe rgb 135 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{135,10,0.0,1.0,0.0}},
{69,"uniform int index 6",SR_U_INT,0,"index",{6}},
{70,"uniform vec3 expect 7 7.1 7.2",SR_U_VEC3,0,"expect",{7,7.1,7.2}},
{71,"draw rect 155 5 10 10",SR_QUAD,0,NULL,{155,5,10,10}},
{72,"probe rgb 160 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{160,10,0.0,1.0,0.0}},
{74,"uniform int index 7",SR_U_INT,0,"index",{7}},
{75,"uniform vec3 expect 8 8.1 8.2",SR_U_VEC3,0,"expect",{8,8.1,8.2}},
{76,"draw rect 180 5 10 10",SR_QUAD,0,NULL,{180,5,10,10}},
{77,"probe rgb 185 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{185,10,0.0,1.0,0.0}},
{35,"clear color 0.5 0.5 0.5 0.5",SR_CLEARCOLOR,0,NULL,{0.5,0.5,0.5,0.5}},
{36,"clear",SR_CLEAR,0,NULL,{0}},
{37,"ortho",SR_ORTHOWINDOW,0,NULL,{0}},
{39,"uniform int index 0",SR_U_INT,0,"index",{0}},
{40,"uniform vec4 expect 1 1.1 1.2 1.3",SR_U_VEC4,0,"expect",{1,1.1,1.2,1.3}},
{41,"draw rect 5 5 10 10",SR_QUAD,0,NULL,{5,5,10,10}},
{42,"probe rgb 10 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{10,10,0.0,1.0,0.0}},
{44,"uniform int index 1",SR_U_INT,0,"index",{1}},
{45,"uniform vec4 expect 2 2.1 2.2 2.3",SR_U_VEC4,0,"expect",{2,2.1,2.2,2.3}},
{46,"draw rect 30 5 10 10",SR_QUAD,0,NULL,{30,5,10,10}},
{47,"probe rgb 35 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{35,10,0.0,1.0,0.0}},
{49,"uniform int index 2",SR_U_INT,0,"index",{2}},
{50,"uniform vec4 expect 3 3.1 3.2 3.3",SR_U_VEC4,0,"expect",{3,3.1,3.2,3.3}},
{51,"draw rect 55 5 10 10",SR_QUAD,0,NULL,{55,5,10,10}},
{52,"probe rgb 60 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{60,10,0.0,1.0,0.0}},
{54,"uniform int index 3",SR_U_INT,0,"index",{3}},
{55,"uniform vec4 expect 4 4.1 4.2 4.3",SR_U_VEC4,0,"expect",{4,4.1,4.2,4.3}},
{56,"draw rect 80 5 10 10",SR_QUAD,0,NULL,{80,5,10,10}},
{57,"probe rgb 85 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{85,10,0.0,1.0,0.0}},
{59,"uniform int index 4",SR_U_INT,0,"index",{4}},
{60,"uniform vec4 expect 5 5.1 5.2 5.3",SR_U_VEC4,0,"expect",{5,5.1,5.2,5.3}},
{61,"draw rect 105 5 10 10",SR_QUAD,0,NULL,{105,5,10,10}},
{62,"probe rgb 110 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{110,10,0.0,1.0,0.0}},
{64,"uniform int index 5",SR_U_INT,0,"index",{5}},
{65,"uniform vec4 expect 6 6.1 6.2 6.3",SR_U_VEC4,0,"expect",{6,6.1,6.2,6.3}},
{66,"draw rect 130 5 10 10",SR_QUAD,0,NULL,{130,5,10,10}},
{67,"probe rgb 135 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{135,10,0.0,1.0,0.0}},
{69,"uniform int index 6",SR_U_INT,0,"index",{6}},
{70,"uniform vec4 expect 7 7.1 7.2 7.3",SR_U_VEC4,0,"expect",{7,7.1,7.2,7.3}},
{71,"draw rect 155 5 10 10",SR_QUAD,0,NULL,{155,5,10,10}},
{72,"probe rgb 160 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{160,10,0.0,1.0,0.0}},
{74,"uniform int index 7",SR_U_INT,0,"index",{7}},
{75,"uniform vec4 expect 8 8.1 8.2 8.3",SR_U_VEC4,0,"expect",{8,8.1,8.2,8.3}},
{76,"draw rect 180 5 10 10",SR_QUAD,0,NULL,{180,5,10,10}},
{77,"probe rgb 185 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{185,10,0.0,1.0,0.0}},
{47,"clear color 0.5 0.5 0.5 0.5",SR_CLEARCOLOR,0,NULL,{0.5,0.5,0.5,0.5}},
{48,"clear",SR_CLEAR,0,NULL,{0}},
{49,"ortho",SR_ORTHOWINDOW,0,NULL,{0}},
{51,"uniform int index 0",SR_U_INT,0,"index",{0}},
{52,"draw rect 5 5 10 10",SR_QUAD,0,NULL,{5,5,10,10}},
{53,"probe rgb 10 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{10,10,0.0,1.0,0.0}},
{55,"uniform int index 1",SR_U_INT,0,"index",{1}},
{56,"draw rect 30 5 10 10",SR_QUAD,0,NULL,{30,5,10,10}},
{57,"probe rgb 35 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{35,10,0.0,1.0,0.0}},
{59,"uniform int index 2",SR_U_INT,0,"index",{2}},
{60,"draw rect 55 5 10 10",SR_QUAD,0,NULL,{55,5,10,10}},
{61,"probe rgb 60 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{60,10,0.0,1.0,0.0}},
{63,"uniform int index 3",SR_U_INT,0,"index",{3}},
{64,"draw rect 80 5 10 10",SR_QUAD,0,NULL,{80,5,10,10}},
{65,"probe rgb 85 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{85,10,0.0,1.0,0.0}},
{67,"uniform int index 4",SR_U_INT,0,"index",{4}},
{68,"draw rect 105 5 10 10",SR_QUAD,0,NULL,{105,5,10,10}},
{69,"probe rgb 110 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{110,10,0.0,1.0,0.0}},
{71,"uniform int index 5",SR_U_INT,0,"index",{5}},
{72,"draw rect 130 5 10 10",SR_QUAD,0,NULL,{130,5,10,10}},
{73,"probe rgb 135 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{135,10,0.0,1.0,0.0}},
{75,"uniform int index 6",SR_U_INT,0,"index",{6}},
{76,"draw rect 155 5 10 10",SR_QUAD,0,NULL,{155,5,10,10}},
{77,"probe rgb 160 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{160,10,0.0,1.0,0.0}},
{79,"uniform int index 7",SR_U_INT,0,"index",{7}},
{80,"draw rect 180 5 10 10",SR_QUAD,0,NULL,{180,5,10,10}},
{81,"probe rgb 185 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{185,10,0.0,1.0,0.0}},
{47,"clear color 0.5 0.5 0.5 0.5",SR_CLEARCOLOR,0,NULL,{0.5,0.5,0.5,0.5}},
{48,"clear",SR_CLEAR,0,NULL,{0}},
{49,"ortho",SR_ORTHOWINDOW,0,NULL,{0}},
{51,"uniform int index 0",SR_U_INT,0,"index",{0}},
{52,"draw rect 5 5 10 10",SR_QUAD,0,NULL,{5,5,10,10}},
{53,"probe rgb 10 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{10,10,0.0,1.0,0.0}},
{55,"uniform int index 1",SR_U_INT,0,"index",{1}},
{56,"draw rect 30 5 10 10",SR_QUAD,0,NULL,{30,5,10,10}},
{57,"probe rgb 35 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{35,10,0.0,1.0,0.0}},
{59,"uniform int index 2",SR_U_INT,0,"index",{2}},
{60,"draw rect 55 5 10 10",SR_QUAD,0,NULL,{55,5,10,10}},
{61,"probe rgb 60 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{60,10,0.0,1.0,0.0}},
{63,"uniform int index 3",SR_U_INT,0,"index",{3}},
{64,"draw rect 80 5 10 10",SR_QUAD,0,NULL,{80,5,10,10}},
{65,"probe rgb 85 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{85,10,0.0,1.0,0.0}},
{67,"uniform int index 4",SR_U_INT,0,"index",{4}},
{68,"draw rect 105 5 10 10",SR_QUAD,0,NULL,{105,5,10,10}},
{69,"probe rgb 110 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{110,10,0.0,1.0,0.0}},
{71,"uniform int index 5",SR_U_INT,0,"index",{5}},
{72,"draw rect 130 5 10 10",SR_QUAD,0,NULL,{130,5,10,10}},
{73,"probe rgb 135 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{135,10,0.0,1.0,0.0}},
{75,"uniform int index 6",SR_U_INT,0,"index",{6}},
{76,"draw rect 155 5 10 10",SR_QUAD,0,NULL,{155,5,10,10}},
{77,"probe rgb 160 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{160,10,0.0,1.0,0.0}},
{79,"uniform int index 7",SR_U_INT,0,"index",{7}},
{80,"draw rect 180 5 10 10",SR_QUAD,0,NULL,{180,5,10,10}},
{81,"probe rgb 185 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{185,10,0.0,1.0,0.0}},
{47,"clear color 0.5 0.5 0.5 0.5",SR_CLEARCOLOR,0,NULL,{0.5,0.5,0.5,0.5}},
{48,"clear",SR_CLEAR,0,NULL,{0}},
{49,"ortho",SR_ORTHOWINDOW,0,NULL,{0}},
{51,"uniform int index 0",SR_U_INT,0,"index",{0}},
{52,"draw rect 5 5 10 10",SR_QUAD,0,NULL,{5,5,10,10}},
{53,"probe rgb 10 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{10,10,0.0,1.0,0.0}},
{55,"uniform int index 1",SR_U_INT,0,"index",{1}},
{56,"draw rect 30 5 10 10",SR_QUAD,0,NULL,{30,5,10,10}},
{57,"probe rgb 35 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{35,10,0.0,1.0,0.0}},
{59,"uniform int index 2",SR_U_INT,0,"index",{2}},
{60,"draw rect 55 5 10 10",SR_QUAD,0,NULL,{55,5,10,10}},
{61,"probe rgb 60 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{60,10,0.0,1.0,0.0}},
{63,"uniform int index 3",SR_U_INT,0,"index",{3}},
{64,"draw rect 80 5 10 10",SR_QUAD,0,NULL,{80,5,10,10}},
{65,"probe rgb 85 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{85,10,0.0,1.0,0.0}},
{67,"uniform int index 4",SR_U_INT,0,"index",{4}},
{68,"draw rect 105 5 10 10",SR_QUAD,0,NULL,{105,5,10,10}},
{69,"probe rgb 110 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{110,10,0.0,1.0,0.0}},
{71,"uniform int index 5",SR_U_INT,0,"index",{5}},
{72,"draw rect 130 5 10 10",SR_QUAD,0,NULL,{130,5,10,10}},
{73,"probe rgb 135 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{135,10,0.0,1.0,0.0}},
{75,"uniform int index 6",SR_U_INT,0,"index",{6}},
{76,"draw rect 155 5 10 10",SR_QUAD,0,NULL,{155,5,10,10}},
{77,"probe rgb 160 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{160,10,0.0,1.0,0.0}},
{79,"uniform int index 7",SR_U_INT,0,"index",{7}},
{80,"draw rect 180 5 10 10",SR_QUAD,0,NULL,{180,5,10,10}},
{81,"probe rgb 185 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{185,10,0.0,1.0,0.0}},
{49,"clear color 0.5 0.5 0.5 0.5",SR_CLEARCOLOR,0,NULL,{0.5,0.5,0.5,0.5}},
{50,"clear",SR_CLEAR,0,NULL,{0}},
{51,"ortho",SR_ORTHOWINDOW,0,NULL,{0}},
{53,"uniform int index 0",SR_U_INT,0,"index",{0}},
{54,"draw rect 5 5 10 10",SR_QUAD,0,NULL,{5,5,10,10}},
{55,"probe rgb 10 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{10,10,0.0,1.0,0.0}},
{57,"uniform int index 1",SR_U_INT,0,"index",{1}},
{58,"draw rect 30 5 10 10",SR_QUAD,0,NULL,{30,5,10,10}},
{59,"probe rgb 35 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{35,10,0.0,1.0,0.0}},
{61,"uniform int index 2",SR_U_INT,0,"index",{2}},
{62,"draw rect 55 5 10 10",SR_QUAD,0,NULL,{55,5,10,10}},
{63,"probe rgb 60 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{60,10,0.0,1.0,0.0}},
{65,"uniform int index 3",SR_U_INT,0,"index",{3}},
{66,"draw rect 80 5 10 10",SR_QUAD,0,NULL,{80,5,10,10}},
{67,"probe rgb 85 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{85,10,0.0,1.0,0.0}},
{69,"uniform int index 4",SR_U_INT,0,"index",{4}},
{70,"draw rect 105 5 10 10",SR_QUAD,0,NULL,{105,5,10,10}},
{71,"probe rgb 110 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{110,10,0.0,1.0,0.0}},
{73,"uniform int index 5",SR_U_INT,0,"index",{5}},
{74,"draw rect 130 5 10 10",SR_QUAD,0,NULL,{130,5,10,10}},
{75,"probe rgb 135 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{135,10,0.0,1.0,0.0}},
{77,"uniform int index 6",SR_U_INT,0,"index",{6}},
{78,"draw rect 155 5 10 10",SR_QUAD,0,NULL,{155,5,10,10}},
{79,"probe rgb 160 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{160,10,0.0,1.0,0.0}},
{81,"uniform int index 7",SR_U_INT,0,"index",{7}},
{82,"draw rect 180 5 10 10",SR_QUAD,0,NULL,{180,5,10,10}},
{83,"probe rgb 185 10 0.0 1.0 0.0",SR_PIX3,0,NULL,{185,10,0.0,1.0,0.0}},
{46,"uniform int b 1",SR_U_INT,0,"b",{1}},
{48,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{49,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{44,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{45,"probe all rgba 0 1 0 1",SR_ALL4,0,NULL,{0,1,0,1}},
{24,"uniform vec4 a 38.2 0.0 1.2 0.0",SR_U_VEC4,0,"a",{38.2,0.0,1.2,0.0}},
{26,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{27,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{26,"uniform int early_return 1",SR_U_INT,0,"early_return",{1}},
{27,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{28,"probe all rgba 0.2 0.2 0.2 0.2",SR_ALL4,0,NULL,{0.2,0.2,0.2,0.2}},
{29,"uniform int early_return 0",SR_U_INT,0,"early_return",{0}},
{30,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{31,"probe all rgba 0.5 0.5 0.5 0.5",SR_ALL4,0,NULL,{0.5,0.5,0.5,0.5}},
{25,"uniform int early_return 1",SR_U_INT,0,"early_return",{1}},
{26,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{27,"probe all rgba 0.5 0.5 0.5 0.5",SR_ALL4,0,NULL,{0.5,0.5,0.5,0.5}},
{28,"uniform int early_return 0",SR_U_INT,0,"early_return",{0}},
{29,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{30,"probe all rgba 0.2 0.2 0.2 0.2",SR_ALL4,0,NULL,{0.2,0.2,0.2,0.2}},
{38,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{39,"probe all rgba 0 1 0 1",SR_ALL4,0,NULL,{0,1,0,1}},
{37,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{38,"probe all rgba 0 1 0 1",SR_ALL4,0,NULL,{0,1,0,1}},
{25,"uniform ivec4 v -2 -1 25 5",SR_U_IVEC4,0,"v",{-2,-1,25,5}},
{27,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{28,"probe all rgba 0 1 0 1",SR_ALL4,0,NULL,{0,1,0,1}},
{42,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{43,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{48,"clear color 0.5 0.5 0.5 0.5",SR_CLEARCOLOR,0,NULL,{0.5,0.5,0.5,0.5}},
{50,"uniform int a 0",SR_U_INT,0,"a",{0}},
{51,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{52,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{54,"uniform int a 1",SR_U_INT,0,"a",{1}},
{55,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{56,"probe all rgba 0.0 0.0 1.0 1.0",SR_ALL4,0,NULL,{0.0,0.0,1.0,1.0}},
{53,"clear color 0.5 0.5 0.5 0.5",SR_CLEARCOLOR,0,NULL,{0.5,0.5,0.5,0.5}},
{55,"uniform int a 0",SR_U_INT,0,"a",{0}},
{56,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{57,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{59,"uniform int a 1",SR_U_INT,0,"a",{1}},
{60,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{61,"probe all rgba 0.0 0.0 1.0 1.0",SR_ALL4,0,NULL,{0.0,0.0,1.0,1.0}},
{51,"clear color 0.5 0.5 0.5 0.5",SR_CLEARCOLOR,0,NULL,{0.5,0.5,0.5,0.5}},
{53,"uniform int loop_count 4",SR_U_INT,0,"loop_count",{4}},
{54,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{55,"probe all rgba 1.0 0.0 0.0 1.0",SR_ALL4,0,NULL,{1.0,0.0,0.0,1.0}},
{57,"uniform int loop_count 3",SR_U_INT,0,"loop_count",{3}},
{58,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{59,"probe all rgba 1.0 0.0 0.0 1.0",SR_ALL4,0,NULL,{1.0,0.0,0.0,1.0}},
{61,"uniform int loop_count 2",SR_U_INT,0,"loop_count",{2}},
{62,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{63,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{65,"uniform int loop_count 1",SR_U_INT,0,"loop_count",{1}},
{66,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{67,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{69,"uniform int loop_count 0",SR_U_INT,0,"loop_count",{0}},
{70,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{71,"probe all rgba 1.0 1.0 1.0 1.0",SR_ALL4,0,NULL,{1.0,1.0,1.0,1.0}},
{46,"clear color 0.5 0.5 0.5 0.5",SR_CLEARCOLOR,0,NULL,{0.5,0.5,0.5,0.5}},
{48,"uniform int loop_count 4",SR_U_INT,0,"loop_count",{4}},
{49,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{50,"probe all rgba 1.0 0.0 0.0 1.0",SR_ALL4,0,NULL,{1.0,0.0,0.0,1.0}},
{52,"uniform int loop_count 3",SR_U_INT,0,"loop_count",{3}},
{53,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{54,"probe all rgba 1.0 0.0 0.0 1.0",SR_ALL4,0,NULL,{1.0,0.0,0.0,1.0}},
{56,"uniform int loop_count 2",SR_U_INT,0,"loop_count",{2}},
{57,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{58,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{60,"uniform int loop_count 1",SR_U_INT,0,"loop_count",{1}},
{61,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{62,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{64,"uniform int loop_count 0",SR_U_INT,0,"loop_count",{0}},
{65,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{66,"probe all rgba 1.0 1.0 1.0 1.0",SR_ALL4,0,NULL,{1.0,1.0,1.0,1.0}},
{57,"clear color 0.5 0.5 0.5 0.5",SR_CLEARCOLOR,0,NULL,{0.5,0.5,0.5,0.5}},
{59,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{60,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{38,"clear color 0.5 0.5 0.5 0.5",SR_CLEARCOLOR,0,NULL,{0.5,0.5,0.5,0.5}},
{40,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{41,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{33,"clear color 0.5 0.5 0.5 0.5",SR_CLEARCOLOR,0,NULL,{0.5,0.5,0.5,0.5}},
{35,"uniform int loop_count 1",SR_U_INT,0,"loop_count",{1}},
{36,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{37,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{39,"uniform int loop_count 0",SR_U_INT,0,"loop_count",{0}},
{40,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{41,"probe all rgba 1.0 0.0 0.0 1.0",SR_ALL4,0,NULL,{1.0,0.0,0.0,1.0}},
{35,"clear color 0.5 0.5 0.5 0.5",SR_CLEARCOLOR,0,NULL,{0.5,0.5,0.5,0.5}},
{37,"uniform int loop_count 1",SR_U_INT,0,"loop_count",{1}},
{38,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{39,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{41,"uniform int loop_count 0",SR_U_INT,0,"loop_count",{0}},
{42,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{43,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{31,"uniform mat2x2 m[0] 0.0 1.0 2.0 3.0",SR_U_MAT2X2,0,"m[0]",{0.0,1.0,2.0,3.0}},
{32,"uniform mat2x2 m[1] 0.0 1.0 2.0 3.0",SR_U_MAT2X2,0,"m[1]",{0.0,1.0,2.0,3.0}},
{33,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{34,"probe all rgba 0 1 0 0",SR_ALL4,0,NULL,{0,1,0,0}},
{33,"uniform mat2x2 m.a 0.0 1.0 2.0 3.0",SR_U_MAT2X2,0,"m.a",{0.0,1.0,2.0,3.0}},
{34,"uniform mat2x2 m.b 0.0 1.0 2.0 3.0",SR_U_MAT2X2,0,"m.b",{0.0,1.0,2.0,3.0}},
{35,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{36,"probe all rgba 0 1 0 0",SR_ALL4,0,NULL,{0,1,0,0}},
{39,"clear color 0.5 0.5 0.5 0.5",SR_CLEARCOLOR,0,NULL,{0.5,0.5,0.5,0.5}},
{41,"uniform int a 1",SR_U_INT,0,"a",{1}},
{42,"uniform int b 1",SR_U_INT,0,"b",{1}},
{43,"uniform int c 1",SR_U_INT,0,"c",{1}},
{45,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{46,"probe all rgba 1.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{1.0,1.0,0.0,1.0}},
{48,"uniform int a 1",SR_U_INT,0,"a",{1}},
{49,"uniform int b 1",SR_U_INT,0,"b",{1}},
{50,"uniform int c 0",SR_U_INT,0,"c",{0}},
{52,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{53,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{39,"clear color 0.5 0.5 0.5 0.5",SR_CLEARCOLOR,0,NULL,{0.5,0.5,0.5,0.5}},
{41,"uniform int a 1",SR_U_INT,0,"a",{1}},
{42,"uniform int b 1",SR_U_INT,0,"b",{1}},
{43,"uniform int c 1",SR_U_INT,0,"c",{1}},
{45,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{46,"probe all rgba 1.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{1.0,1.0,0.0,1.0}},
{48,"uniform int a 1",SR_U_INT,0,"a",{1}},
{49,"uniform int b 1",SR_U_INT,0,"b",{1}},
{50,"uniform int c 0",SR_U_INT,0,"c",{0}},
{52,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{53,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{39,"clear color 0.5 0.5 0.5 0.5",SR_CLEARCOLOR,0,NULL,{0.5,0.5,0.5,0.5}},
{41,"uniform int a 1",SR_U_INT,0,"a",{1}},
{42,"uniform int b 1",SR_U_INT,0,"b",{1}},
{43,"uniform int c 1",SR_U_INT,0,"c",{1}},
{45,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{46,"probe all rgba 1.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{1.0,1.0,0.0,1.0}},
{48,"uniform int a 1",SR_U_INT,0,"a",{1}},
{49,"uniform int b 1",SR_U_INT,0,"b",{1}},
{50,"uniform int c 0",SR_U_INT,0,"c",{0}},
{52,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{53,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{39,"clear color 0.5 0.5 0.5 0.5",SR_CLEARCOLOR,0,NULL,{0.5,0.5,0.5,0.5}},
{41,"uniform int a 1",SR_U_INT,0,"a",{1}},
{42,"uniform int b 1",SR_U_INT,0,"b",{1}},
{43,"uniform int c 1",SR_U_INT,0,"c",{1}},
{45,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{46,"probe all rgba 1.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{1.0,1.0,0.0,1.0}},
{48,"uniform int a 1",SR_U_INT,0,"a",{1}},
{49,"uniform int b 1",SR_U_INT,0,"b",{1}},
{50,"uniform int c 0",SR_U_INT,0,"c",{0}},
{52,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{53,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{27,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{28,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{24,"uniform vec4 a 0.0 38.2 0.0 1.2",SR_U_VEC4,0,"a",{0.0,38.2,0.0,1.2}},
{26,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{27,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{23,"uniform vec4 v -2 -1 1 2",SR_U_VEC4,0,"v",{-2,-1,1,2}},
{25,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{26,"probe all rgba 0.375 0.5 0.75 0.75",SR_ALL4,0,NULL,{0.375,0.5,0.75,0.75}},
{24,"uniform vec4 x 0.5 2.0 0.5 1.0",SR_U_VEC4,0,"x",{0.5,2.0,0.5,1.0}},
{25,"uniform vec4 y 2.0 0.5 -1.0 1.0",SR_U_VEC4,0,"y",{2.0,0.5,-1.0,1.0}},
{27,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{28,"probe all rgba 0.375 0.75 0.75 0.75",SR_ALL4,0,NULL,{0.375,0.75,0.75,0.75}},
{23,"uniform vec4 v 0.25 1.0 2.0 0.25",SR_U_VEC4,0,"v",{0.25,1.0,2.0,0.25}},
{25,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{26,"probe all rgba 0.5 0.75 0.75 0.5",SR_ALL4,0,NULL,{0.5,0.75,0.75,0.5}},
{20,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{21,"probe rgba 1 1 1.0 0.2 0.3 0.4",SR_PIX4,0,NULL,{1,1,1.0,0.2,0.3,0.4}},
{20,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{21,"probe rgba 1 1 0.1 0.2 0.3 1.0",SR_PIX4,0,NULL,{1,1,0.1,0.2,0.3,1.0}},
{20,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{21,"probe rgb 1 1 0.0 1.0 0.0",SR_PIX3,0,NULL,{1,1,0.0,1.0,0.0}},
{19,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{20,"probe rgb 1 1 0.0 1.0 0.0",SR_PIX3,0,NULL,{1,1,0.0,1.0,0.0}},
{26,"uniform int early_return 1",SR_U_INT,0,"early_return",{1}},
{27,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{28,"probe all rgba 0.5 0.5 0.5 0.5",SR_ALL4,0,NULL,{0.5,0.5,0.5,0.5}},
{29,"uniform int early_return 0",SR_U_INT,0,"early_return",{0}},
{30,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{31,"probe all rgba 0.2 0.2 0.2 0.2",SR_ALL4,0,NULL,{0.2,0.2,0.2,0.2}},
{25,"uniform int early_return 1",SR_U_INT,0,"early_return",{1}},
{26,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{27,"probe all rgba 0.5 0.5 0.5 0.5",SR_ALL4,0,NULL,{0.5,0.5,0.5,0.5}},
{28,"uniform int early_return 0",SR_U_INT,0,"early_return",{0}},
{29,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{30,"probe all rgba 0.2 0.2 0.2 0.2",SR_ALL4,0,NULL,{0.2,0.2,0.2,0.2}},
{25,"uniform int early_return 1",SR_U_INT,0,"early_return",{1}},
{26,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{27,"probe all rgba 0.5 0.5 0.5 0.5",SR_ALL4,0,NULL,{0.5,0.5,0.5,0.5}},
{28,"uniform int early_return 0",SR_U_INT,0,"early_return",{0}},
{29,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{30,"probe all rgba 0.2 0.2 0.2 0.2",SR_ALL4,0,NULL,{0.2,0.2,0.2,0.2}},
{40,"uniform int three 3",SR_U_INT,0,"three",{3}},
{41,"uniform float five 5.0",SR_U_FLOAT,0,"five",{5.0}},
{42,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{43,"probe all rgba 0.0 1.0 0.0 0.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,0.0}},
{34,"uniform vec4 data 0.2 0.4 0.6 0.8",SR_U_VEC4,0,"data",{0.2,0.4,0.6,0.8}},
{36,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{37,"probe all rgba 0.2 0.6 0.8 1.0",SR_ALL4,0,NULL,{0.2,0.6,0.8,1.0}},
{14,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{15,"relative probe rgba (0.5, 0.5) (0.5, 1.0, 0.25, 1.0)",SR_REL4,0,NULL,{0.5,0.5,0.5,1.0,0.25,1.0}},
{15,"clear depth 1.0",SR_CLEARZ,0,NULL,{1.0}},
{16,"clear",SR_CLEAR,0,NULL,{0}},
{17,"enable GL_DEPTH_TEST",SR_ENABLE,GL_DEPTH_TEST,NULL,{0}},
{18,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{19,"probe depth 0 0 1.0",SR_ZPIX,0,NULL,{0,0,1.0}},
{20,"probe depth 9 0 1.0",SR_ZPIX,0,NULL,{9,0,1.0}},
{21,"probe depth 10 0 0.5",SR_ZPIX,0,NULL,{10,0,0.5}},
{22,"probe depth 11 0 0.5",SR_ZPIX,0,NULL,{11,0,0.5}},
{32,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{33,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{18,"link error",SR_LINKERR,0,NULL,{0}},
{21,"link error",SR_LINKERR,0,NULL,{0}},
{21,"link success",SR_LINKOK,0,NULL,{0}},
{25,"link error",SR_LINKERR,0,NULL,{0}},
{21,"link error",SR_LINKERR,0,NULL,{0}},
{29,"link error",SR_LINKERR,0,NULL,{0}},
{27,"link error",SR_LINKERR,0,NULL,{0}},
{21,"link success",SR_LINKOK,0,NULL,{0}},
{19,"link success",SR_LINKOK,0,NULL,{0}},
{26,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{27,"probe all rgb 0.0 1.0 0.0",SR_ALL3,0,NULL,{0.0,1.0,0.0}},
{31,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{32,"probe all rgb 0.0 1.0 0.0",SR_ALL3,0,NULL,{0.0,1.0,0.0}},
{27,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{28,"probe all rgb 0.0 1.0 0.0",SR_ALL3,0,NULL,{0.0,1.0,0.0}},
{34,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{35,"probe all rgb 0.0 1.0 0.0",SR_ALL3,0,NULL,{0.0,1.0,0.0}},
{36,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{37,"probe all rgb 0.0 1.0 0.0",SR_ALL3,0,NULL,{0.0,1.0,0.0}},
{29,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{30,"probe all rgb 0.0 1.0 0.0",SR_ALL3,0,NULL,{0.0,1.0,0.0}},
{39,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{40,"probe all rgb 0.0 1.0 0.0",SR_ALL3,0,NULL,{0.0,1.0,0.0}},
{41,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{42,"probe all rgb 0.0 1.0 0.0",SR_ALL3,0,NULL,{0.0,1.0,0.0}},
{28,"uniform vec4 v 0.0 -1.0 0.0 1.0",SR_U_VEC4,0,"v",{0.0,-1.0,0.0,1.0}},
{29,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{30,"probe all rgb 0.0 1.0 0.0",SR_ALL3,0,NULL,{0.0,1.0,0.0}},
{33,"uniform vec4 v 1.0 0.0 0.0 1.0",SR_U_VEC4,0,"v",{1.0,0.0,0.0,1.0}},
{34,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{35,"probe all rgb 0.0 1.0 0.0",SR_ALL3,0,NULL,{0.0,1.0,0.0}},
{29,"uniform vec4 v 0.0 -1.0 0.0 1.0",SR_U_VEC4,0,"v",{0.0,-1.0,0.0,1.0}},
{30,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{31,"probe all rgb 0.0 1.0 0.0",SR_ALL3,0,NULL,{0.0,1.0,0.0}},
{35,"uniform vec4 v 1.0 0.0 0.0 1.0",SR_U_VEC4,0,"v",{1.0,0.0,0.0,1.0}},
{36,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{37,"probe all rgb 0.0 1.0 0.0",SR_ALL3,0,NULL,{0.0,1.0,0.0}},
{38,"uniform float f -1.0",SR_U_FLOAT,0,"f",{-1.0}},
{39,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{40,"probe all rgb 0.0 1.0 0.0",SR_ALL3,0,NULL,{0.0,1.0,0.0}},
{31,"uniform vec4 v 0.0 -1.0 0.0 1.0",SR_U_VEC4,0,"v",{0.0,-1.0,0.0,1.0}},
{32,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{33,"probe all rgb 0.0 1.0 0.0",SR_ALL3,0,NULL,{0.0,1.0,0.0}},
{41,"uniform float f -1.0",SR_U_FLOAT,0,"f",{-1.0}},
{42,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{43,"probe all rgb 0.0 1.0 0.0",SR_ALL3,0,NULL,{0.0,1.0,0.0}},
{43,"uniform float f -1.0",SR_U_FLOAT,0,"f",{-1.0}},
{44,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{45,"probe all rgb 0.0 1.0 0.0",SR_ALL3,0,NULL,{0.0,1.0,0.0}},
{26,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{27,"relative probe rgba (0.5, 0.5) (0.5, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.5,0.5,0.5,0.0,0.0,0.0}},
{28,"link error",SR_LINKERR,0,NULL,{0}},
{21,"link error",SR_LINKERR,0,NULL,{0}},
{22,"link error",SR_LINKERR,0,NULL,{0}},
{21,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{22,"relative probe rgba (0.5, 0.5) (0.0, 1.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.5,0.5,0.0,1.0,0.0,0.0}},
{21,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{22,"relative probe rgba (0.5, 0.5) (1.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.5,0.5,1.0,0.0,0.0,0.0}},
{21,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{22,"relative probe rgba (0.5, 0.5) (0.0, 1.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.5,0.5,0.0,1.0,0.0,0.0}},
{21,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{22,"relative probe rgba (0.5, 0.5) (0.0, 1.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.5,0.5,0.0,1.0,0.0,0.0}},
{18,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{19,"relative probe rgba (0.5, 0.5) (0.5, 0.5, 0.5, 0.5)",SR_REL4,0,NULL,{0.5,0.5,0.5,0.5,0.5,0.5}},
{17,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{18,"relative probe rgba (0.5, 0.5) (0.3, 0.3, 0.3, 0.3)",SR_REL4,0,NULL,{0.5,0.5,0.3,0.3,0.3,0.3}},
{17,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{18,"relative probe rgba (0.5, 0.5) (0.25, 0.25, 0.25, 0.25)",SR_REL4,0,NULL,{0.5,0.5,0.25,0.25,0.25,0.25}},
{18,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{19,"relative probe rgba (0.5, 0.5) (0.25, 0.25, 0.25, 0.25)",SR_REL4,0,NULL,{0.5,0.5,0.25,0.25,0.25,0.25}},
{17,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{18,"relative probe rgba (0.5, 0.5) (0.5, 0.5, 0.5, 0.5)",SR_REL4,0,NULL,{0.5,0.5,0.5,0.5,0.5,0.5}},
{18,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{19,"relative probe rgba (0.5, 0.5) (0.0, 1.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.5,0.5,0.0,1.0,0.0,0.0}},
{18,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{19,"relative probe rgba (0.5, 0.5) (0.5, 0.5, 0.5, 0.5)",SR_REL4,0,NULL,{0.5,0.5,0.5,0.5,0.5,0.5}},
{18,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{19,"relative probe rgba (0.5, 0.5) (0.0, 1.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.5,0.5,0.0,1.0,0.0,0.0}},
{18,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{19,"relative probe rgba (0.5, 0.5) (1.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.5,0.5,1.0,0.0,0.0,0.0}},
{14,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{15,"relative probe rgba (0.5, 0.5) (0.5, 0.5, 0.5, 0.5)",SR_REL4,0,NULL,{0.5,0.5,0.5,0.5,0.5,0.5}},
{24,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{25,"relative probe rgba (0.5, 0.5) (0.0, 1.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.5,0.5,0.0,1.0,0.0,0.0}},
{24,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{25,"relative probe rgba (0.5, 0.5) (0.0, 1.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.5,0.5,0.0,1.0,0.0,0.0}},
{26,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{27,"relative probe rgba (0.5, 0.5) (1.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.5,0.5,1.0,0.0,0.0,0.0}},
{18,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{19,"relative probe rgba (0.5, 0.5) (1.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.5,0.5,1.0,0.0,0.0,0.0}},
{25,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{26,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{31,"clip plane 0 0 1 0 0.5",SR_PLANE,0,NULL,{0,0,1,0,0.5}},
{32,"clip plane 1 -1 1 0 0.8",SR_PLANE,0,NULL,{1,-1,1,0,0.8}},
{33,"clip plane 2 -1 -1 0 0.8",SR_PLANE,0,NULL,{2,-1,-1,0,0.8}},
{34,"clip plane 3 0 -1 0 0.5",SR_PLANE,0,NULL,{3,0,-1,0,0.5}},
{35,"clip plane 4 1 -1 0 0.8",SR_PLANE,0,NULL,{4,1,-1,0,0.8}},
{36,"clip plane 5 1 1 0 0.8",SR_PLANE,0,NULL,{5,1,1,0,0.8}},
{37,"clear color 0.0 0.0 0.0 0.0",SR_CLEARCOLOR,0,NULL,{0.0,0.0,0.0,0.0}},
{40,"enable GL_CLIP_PLANE0",SR_ENABLE,GL_CLIP_PLANE0,NULL,{0}},
{41,"disable GL_CLIP_PLANE1",SR_DISABLE,GL_CLIP_PLANE1,NULL,{0}},
{42,"enable GL_CLIP_PLANE2",SR_ENABLE,GL_CLIP_PLANE2,NULL,{0}},
{43,"disable GL_CLIP_PLANE3",SR_DISABLE,GL_CLIP_PLANE3,NULL,{0}},
{44,"enable GL_CLIP_PLANE4",SR_ENABLE,GL_CLIP_PLANE4,NULL,{0}},
{45,"disable GL_CLIP_PLANE5",SR_DISABLE,GL_CLIP_PLANE5,NULL,{0}},
{46,"clear",SR_CLEAR,0,NULL,{0}},
{47,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{50,"relative probe rgba (0.3, 0.4) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.3,0.4,1.0,1.0,1.0,1.0}},
{51,"relative probe rgba (0.5, 0.3) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.3,1.0,1.0,1.0,1.0}},
{52,"relative probe rgba (0.7, 0.4) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.7,0.4,1.0,1.0,1.0,1.0}},
{53,"relative probe rgba (0.7, 0.6) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.7,0.6,1.0,1.0,1.0,1.0}},
{54,"relative probe rgba (0.5, 0.7) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.7,1.0,1.0,1.0,1.0}},
{55,"relative probe rgba (0.3, 0.6) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.3,0.6,1.0,1.0,1.0,1.0}},
{58,"relative probe rgba (0.5, 0.2) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.5,0.2,0.0,0.0,0.0,0.0}},
{59,"relative probe rgba (0.8, 0.3) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.8,0.3,1.0,1.0,1.0,1.0}},
{60,"relative probe rgba (0.8, 0.7) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.8,0.7,0.0,0.0,0.0,0.0}},
{61,"relative probe rgba (0.5, 0.8) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.8,1.0,1.0,1.0,1.0}},
{62,"relative probe rgba (0.2, 0.7) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.2,0.7,0.0,0.0,0.0,0.0}},
{63,"relative probe rgba (0.2, 0.3) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.2,0.3,1.0,1.0,1.0,1.0}},
{66,"enable GL_CLIP_PLANE0",SR_ENABLE,GL_CLIP_PLANE0,NULL,{0}},
{67,"enable GL_CLIP_PLANE1",SR_ENABLE,GL_CLIP_PLANE1,NULL,{0}},
{68,"disable GL_CLIP_PLANE2",SR_DISABLE,GL_CLIP_PLANE2,NULL,{0}},
{69,"disable GL_CLIP_PLANE3",SR_DISABLE,GL_CLIP_PLANE3,NULL,{0}},
{70,"enable GL_CLIP_PLANE4",SR_ENABLE,GL_CLIP_PLANE4,NULL,{0}},
{71,"enable GL_CLIP_PLANE5",SR_ENABLE,GL_CLIP_PLANE5,NULL,{0}},
{72,"clear",SR_CLEAR,0,NULL,{0}},
{73,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{76,"relative probe rgba (0.3, 0.4) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.3,0.4,1.0,1.0,1.0,1.0}},
{77,"relative probe rgba (0.5, 0.3) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.3,1.0,1.0,1.0,1.0}},
{78,"relative probe rgba (0.7, 0.4) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.7,0.4,1.0,1.0,1.0,1.0}},
{79,"relative probe rgba (0.7, 0.6) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.7,0.6,1.0,1.0,1.0,1.0}},
{80,"relative probe rgba (0.5, 0.7) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.7,1.0,1.0,1.0,1.0}},
{81,"relative probe rgba (0.3, 0.6) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.3,0.6,1.0,1.0,1.0,1.0}},
{84,"relative probe rgba (0.5, 0.2) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.5,0.2,0.0,0.0,0.0,0.0}},
{85,"relative probe rgba (0.8, 0.3) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.8,0.3,0.0,0.0,0.0,0.0}},
{86,"relative probe rgba (0.8, 0.7) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.8,0.7,1.0,1.0,1.0,1.0}},
{87,"relative probe rgba (0.5, 0.8) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.8,1.0,1.0,1.0,1.0}},
{88,"relative probe rgba (0.2, 0.7) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.2,0.7,0.0,0.0,0.0,0.0}},
{89,"relative probe rgba (0.2, 0.3) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.2,0.3,0.0,0.0,0.0,0.0}},
{92,"enable GL_CLIP_PLANE0",SR_ENABLE,GL_CLIP_PLANE0,NULL,{0}},
{93,"enable GL_CLIP_PLANE1",SR_ENABLE,GL_CLIP_PLANE1,NULL,{0}},
{94,"enable GL_CLIP_PLANE2",SR_ENABLE,GL_CLIP_PLANE2,NULL,{0}},
{95,"enable GL_CLIP_PLANE3",SR_ENABLE,GL_CLIP_PLANE3,NULL,{0}},
{96,"disable GL_CLIP_PLANE4",SR_DISABLE,GL_CLIP_PLANE4,NULL,{0}},
{97,"disable GL_CLIP_PLANE5",SR_DISABLE,GL_CLIP_PLANE5,NULL,{0}},
{98,"clear",SR_CLEAR,0,NULL,{0}},
{99,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{102,"relative probe rgba (0.3, 0.4) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.3,0.4,1.0,1.0,1.0,1.0}},
{103,"relative probe rgba (0.5, 0.3) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.3,1.0,1.0,1.0,1.0}},
{104,"relative probe rgba (0.7, 0.4) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.7,0.4,1.0,1.0,1.0,1.0}},
{105,"relative probe rgba (0.7, 0.6) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.7,0.6,1.0,1.0,1.0,1.0}},
{106,"relative probe rgba (0.5, 0.7) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.7,1.0,1.0,1.0,1.0}},
{107,"relative probe rgba (0.3, 0.6) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.3,0.6,1.0,1.0,1.0,1.0}},
{110,"relative probe rgba (0.5, 0.2) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.5,0.2,0.0,0.0,0.0,0.0}},
{111,"relative probe rgba (0.8, 0.3) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.8,0.3,0.0,0.0,0.0,0.0}},
{112,"relative probe rgba (0.8, 0.7) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.8,0.7,0.0,0.0,0.0,0.0}},
{113,"relative probe rgba (0.5, 0.8) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.5,0.8,0.0,0.0,0.0,0.0}},
{114,"relative probe rgba (0.2, 0.7) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.2,0.7,1.0,1.0,1.0,1.0}},
{115,"relative probe rgba (0.2, 0.3) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.2,0.3,1.0,1.0,1.0,1.0}},
{118,"disable GL_CLIP_PLANE0",SR_DISABLE,GL_CLIP_PLANE0,NULL,{0}},
{119,"disable GL_CLIP_PLANE1",SR_DISABLE,GL_CLIP_PLANE1,NULL,{0}},
{120,"disable GL_CLIP_PLANE2",SR_DISABLE,GL_CLIP_PLANE2,NULL,{0}},
{121,"disable GL_CLIP_PLANE3",SR_DISABLE,GL_CLIP_PLANE3,NULL,{0}},
{122,"enable GL_CLIP_PLANE4",SR_ENABLE,GL_CLIP_PLANE4,NULL,{0}},
{123,"enable GL_CLIP_PLANE5",SR_ENABLE,GL_CLIP_PLANE5,NULL,{0}},
{124,"clear",SR_CLEAR,0,NULL,{0}},
{125,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{128,"relative probe rgba (0.3, 0.4) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.3,0.4,1.0,1.0,1.0,1.0}},
{129,"relative probe rgba (0.5, 0.3) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.3,1.0,1.0,1.0,1.0}},
{130,"relative probe rgba (0.7, 0.4) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.7,0.4,1.0,1.0,1.0,1.0}},
{131,"relative probe rgba (0.7, 0.6) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.7,0.6,1.0,1.0,1.0,1.0}},
{132,"relative probe rgba (0.5, 0.7) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.7,1.0,1.0,1.0,1.0}},
{133,"relative probe rgba (0.3, 0.6) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.3,0.6,1.0,1.0,1.0,1.0}},
{136,"relative probe rgba (0.5, 0.2) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.2,1.0,1.0,1.0,1.0}},
{137,"relative probe rgba (0.8, 0.3) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.8,0.3,1.0,1.0,1.0,1.0}},
{138,"relative probe rgba (0.8, 0.7) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.8,0.7,1.0,1.0,1.0,1.0}},
{139,"relative probe rgba (0.5, 0.8) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.8,1.0,1.0,1.0,1.0}},
{140,"relative probe rgba (0.2, 0.7) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.2,0.7,0.0,0.0,0.0,0.0}},
{141,"relative probe rgba (0.2, 0.3) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.2,0.3,0.0,0.0,0.0,0.0}},
{37,"clear color 0.0 0.0 0.0 0.0",SR_CLEARCOLOR,0,NULL,{0.0,0.0,0.0,0.0}},
{38,"clear",SR_CLEAR,0,NULL,{0}},
{39,"clip plane 0 1.0 0.0 0.0 0.0",SR_PLANE,0,NULL,{0,1.0,0.0,0.0,0.0}},
{40,"enable GL_CLIP_PLANE0",SR_ENABLE,GL_CLIP_PLANE0,NULL,{0}},
{41,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{42,"probe all rgba 1.0 1.0 1.0 1.0",SR_ALL4,0,NULL,{1.0,1.0,1.0,1.0}},
{37,"clear color 0.0 0.0 0.0 0.0",SR_CLEARCOLOR,0,NULL,{0.0,0.0,0.0,0.0}},
{38,"clear",SR_CLEAR,0,NULL,{0}},
{39,"clip plane 0 1.0 0.0 0.0 0.0",SR_PLANE,0,NULL,{0,1.0,0.0,0.0,0.0}},
{40,"enable GL_CLIP_PLANE0",SR_ENABLE,GL_CLIP_PLANE0,NULL,{0}},
{41,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{42,"probe all rgba 0.0 0.0 0.0 0.0",SR_ALL4,0,NULL,{0.0,0.0,0.0,0.0}},
{31,"clear color 0.0 0.0 0.0 0.0",SR_CLEARCOLOR,0,NULL,{0.0,0.0,0.0,0.0}},
{32,"clear",SR_CLEAR,0,NULL,{0}},
{33,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{34,"clip plane 0 0 1 0 -2.5",SR_PLANE,0,NULL,{0,0,1,0,-2.5}},
{35,"clip plane 1 -1 1 0 4",SR_PLANE,0,NULL,{1,-1,1,0,4}},
{36,"clip plane 2 -1 -1 0 14",SR_PLANE,0,NULL,{2,-1,-1,0,14}},
{37,"clip plane 3 0 -1 0 7.5",SR_PLANE,0,NULL,{3,0,-1,0,7.5}},
{38,"clip plane 4 1 -1 0 4",SR_PLANE,0,NULL,{4,1,-1,0,4}},
{39,"clip plane 5 1 1 0 -6",SR_PLANE,0,NULL,{5,1,1,0,-6}},
{40,"enable GL_CLIP_PLANE0",SR_ENABLE,GL_CLIP_PLANE0,NULL,{0}},
{41,"enable GL_CLIP_PLANE1",SR_ENABLE,GL_CLIP_PLANE1,NULL,{0}},
{42,"enable GL_CLIP_PLANE2",SR_ENABLE,GL_CLIP_PLANE2,NULL,{0}},
{43,"enable GL_CLIP_PLANE3",SR_ENABLE,GL_CLIP_PLANE3,NULL,{0}},
{44,"enable GL_CLIP_PLANE4",SR_ENABLE,GL_CLIP_PLANE4,NULL,{0}},
{45,"enable GL_CLIP_PLANE5",SR_ENABLE,GL_CLIP_PLANE5,NULL,{0}},
{46,"draw rect 0.1 0.1 0.8 0.8",SR_QUAD,0,NULL,{0.1,0.1,0.8,0.8}},
{49,"relative probe rgba (0.3, 0.4) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.3,0.4,1.0,1.0,1.0,1.0}},
{50,"relative probe rgba (0.5, 0.3) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.3,1.0,1.0,1.0,1.0}},
{51,"relative probe rgba (0.7, 0.4) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.7,0.4,1.0,1.0,1.0,1.0}},
{52,"relative probe rgba (0.7, 0.6) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.7,0.6,1.0,1.0,1.0,1.0}},
{53,"relative probe rgba (0.5, 0.7) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.7,1.0,1.0,1.0,1.0}},
{54,"relative probe rgba (0.3, 0.6) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.3,0.6,1.0,1.0,1.0,1.0}},
{57,"relative probe rgba (0.2, 0.3) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.2,0.3,0.0,0.0,0.0,0.0}},
{58,"relative probe rgba (0.5, 0.2) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.5,0.2,0.0,0.0,0.0,0.0}},
{59,"relative probe rgba (0.8, 0.3) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.8,0.3,0.0,0.0,0.0,0.0}},
{60,"relative probe rgba (0.8, 0.7) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.8,0.7,0.0,0.0,0.0,0.0}},
{61,"relative probe rgba (0.5, 0.8) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.5,0.8,0.0,0.0,0.0,0.0}},
{62,"relative probe rgba (0.2, 0.7) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.2,0.7,0.0,0.0,0.0,0.0}},
{37,"clip plane 0 0 1 0 0.5",SR_PLANE,0,NULL,{0,0,1,0,0.5}},
{38,"clip plane 1 -1 1 0 0.8",SR_PLANE,0,NULL,{1,-1,1,0,0.8}},
{39,"clip plane 2 -1 -1 0 0.8",SR_PLANE,0,NULL,{2,-1,-1,0,0.8}},
{40,"clip plane 3 0 -1 0 0.5",SR_PLANE,0,NULL,{3,0,-1,0,0.5}},
{41,"clip plane 4 1 -1 0 0.8",SR_PLANE,0,NULL,{4,1,-1,0,0.8}},
{42,"clip plane 5 1 1 0 0.8",SR_PLANE,0,NULL,{5,1,1,0,0.8}},
{43,"clear color 0.0 0.0 0.0 0.0",SR_CLEARCOLOR,0,NULL,{0.0,0.0,0.0,0.0}},
{46,"enable GL_CLIP_PLANE0",SR_ENABLE,GL_CLIP_PLANE0,NULL,{0}},
{47,"disable GL_CLIP_PLANE1",SR_DISABLE,GL_CLIP_PLANE1,NULL,{0}},
{48,"enable GL_CLIP_PLANE2",SR_ENABLE,GL_CLIP_PLANE2,NULL,{0}},
{49,"disable GL_CLIP_PLANE3",SR_DISABLE,GL_CLIP_PLANE3,NULL,{0}},
{50,"enable GL_CLIP_PLANE4",SR_ENABLE,GL_CLIP_PLANE4,NULL,{0}},
{51,"disable GL_CLIP_PLANE5",SR_DISABLE,GL_CLIP_PLANE5,NULL,{0}},
{52,"clear",SR_CLEAR,0,NULL,{0}},
{53,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{56,"relative probe rgba (0.3, 0.4) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.3,0.4,1.0,1.0,1.0,1.0}},
{57,"relative probe rgba (0.5, 0.3) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.3,1.0,1.0,1.0,1.0}},
{58,"relative probe rgba (0.7, 0.4) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.7,0.4,1.0,1.0,1.0,1.0}},
{59,"relative probe rgba (0.7, 0.6) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.7,0.6,1.0,1.0,1.0,1.0}},
{60,"relative probe rgba (0.5, 0.7) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.7,1.0,1.0,1.0,1.0}},
{61,"relative probe rgba (0.3, 0.6) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.3,0.6,1.0,1.0,1.0,1.0}},
{64,"relative probe rgba (0.5, 0.2) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.5,0.2,0.0,0.0,0.0,0.0}},
{65,"relative probe rgba (0.8, 0.3) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.8,0.3,1.0,1.0,1.0,1.0}},
{66,"relative probe rgba (0.8, 0.7) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.8,0.7,0.0,0.0,0.0,0.0}},
{67,"relative probe rgba (0.5, 0.8) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.8,1.0,1.0,1.0,1.0}},
{68,"relative probe rgba (0.2, 0.7) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.2,0.7,0.0,0.0,0.0,0.0}},
{69,"relative probe rgba (0.2, 0.3) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.2,0.3,1.0,1.0,1.0,1.0}},
{72,"enable GL_CLIP_PLANE0",SR_ENABLE,GL_CLIP_PLANE0,NULL,{0}},
{73,"enable GL_CLIP_PLANE1",SR_ENABLE,GL_CLIP_PLANE1,NULL,{0}},
{74,"disable GL_CLIP_PLANE2",SR_DISABLE,GL_CLIP_PLANE2,NULL,{0}},
{75,"disable GL_CLIP_PLANE3",SR_DISABLE,GL_CLIP_PLANE3,NULL,{0}},
{76,"enable GL_CLIP_PLANE4",SR_ENABLE,GL_CLIP_PLANE4,NULL,{0}},
{77,"enable GL_CLIP_PLANE5",SR_ENABLE,GL_CLIP_PLANE5,NULL,{0}},
{78,"clear",SR_CLEAR,0,NULL,{0}},
{79,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{82,"relative probe rgba (0.3, 0.4) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.3,0.4,1.0,1.0,1.0,1.0}},
{83,"relative probe rgba (0.5, 0.3) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.3,1.0,1.0,1.0,1.0}},
{84,"relative probe rgba (0.7, 0.4) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.7,0.4,1.0,1.0,1.0,1.0}},
{85,"relative probe rgba (0.7, 0.6) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.7,0.6,1.0,1.0,1.0,1.0}},
{86,"relative probe rgba (0.5, 0.7) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.7,1.0,1.0,1.0,1.0}},
{87,"relative probe rgba (0.3, 0.6) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.3,0.6,1.0,1.0,1.0,1.0}},
{90,"relative probe rgba (0.5, 0.2) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.5,0.2,0.0,0.0,0.0,0.0}},
{91,"relative probe rgba (0.8, 0.3) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.8,0.3,0.0,0.0,0.0,0.0}},
{92,"relative probe rgba (0.8, 0.7) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.8,0.7,1.0,1.0,1.0,1.0}},
{93,"relative probe rgba (0.5, 0.8) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.8,1.0,1.0,1.0,1.0}},
{94,"relative probe rgba (0.2, 0.7) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.2,0.7,0.0,0.0,0.0,0.0}},
{95,"relative probe rgba (0.2, 0.3) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.2,0.3,0.0,0.0,0.0,0.0}},
{98,"enable GL_CLIP_PLANE0",SR_ENABLE,GL_CLIP_PLANE0,NULL,{0}},
{99,"enable GL_CLIP_PLANE1",SR_ENABLE,GL_CLIP_PLANE1,NULL,{0}},
{100,"enable GL_CLIP_PLANE2",SR_ENABLE,GL_CLIP_PLANE2,NULL,{0}},
{101,"enable GL_CLIP_PLANE3",SR_ENABLE,GL_CLIP_PLANE3,NULL,{0}},
{102,"disable GL_CLIP_PLANE4",SR_DISABLE,GL_CLIP_PLANE4,NULL,{0}},
{103,"disable GL_CLIP_PLANE5",SR_DISABLE,GL_CLIP_PLANE5,NULL,{0}},
{104,"clear",SR_CLEAR,0,NULL,{0}},
{105,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{108,"relative probe rgba (0.3, 0.4) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.3,0.4,1.0,1.0,1.0,1.0}},
{109,"relative probe rgba (0.5, 0.3) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.3,1.0,1.0,1.0,1.0}},
{110,"relative probe rgba (0.7, 0.4) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.7,0.4,1.0,1.0,1.0,1.0}},
{111,"relative probe rgba (0.7, 0.6) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.7,0.6,1.0,1.0,1.0,1.0}},
{112,"relative probe rgba (0.5, 0.7) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.7,1.0,1.0,1.0,1.0}},
{113,"relative probe rgba (0.3, 0.6) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.3,0.6,1.0,1.0,1.0,1.0}},
{116,"relative probe rgba (0.5, 0.2) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.5,0.2,0.0,0.0,0.0,0.0}},
{117,"relative probe rgba (0.8, 0.3) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.8,0.3,0.0,0.0,0.0,0.0}},
{118,"relative probe rgba (0.8, 0.7) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.8,0.7,0.0,0.0,0.0,0.0}},
{119,"relative probe rgba (0.5, 0.8) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.5,0.8,0.0,0.0,0.0,0.0}},
{120,"relative probe rgba (0.2, 0.7) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.2,0.7,1.0,1.0,1.0,1.0}},
{121,"relative probe rgba (0.2, 0.3) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.2,0.3,1.0,1.0,1.0,1.0}},
{124,"disable GL_CLIP_PLANE0",SR_DISABLE,GL_CLIP_PLANE0,NULL,{0}},
{125,"disable GL_CLIP_PLANE1",SR_DISABLE,GL_CLIP_PLANE1,NULL,{0}},
{126,"disable GL_CLIP_PLANE2",SR_DISABLE,GL_CLIP_PLANE2,NULL,{0}},
{127,"disable GL_CLIP_PLANE3",SR_DISABLE,GL_CLIP_PLANE3,NULL,{0}},
{128,"enable GL_CLIP_PLANE4",SR_ENABLE,GL_CLIP_PLANE4,NULL,{0}},
{129,"enable GL_CLIP_PLANE5",SR_ENABLE,GL_CLIP_PLANE5,NULL,{0}},
{130,"clear",SR_CLEAR,0,NULL,{0}},
{131,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{134,"relative probe rgba (0.3, 0.4) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.3,0.4,1.0,1.0,1.0,1.0}},
{135,"relative probe rgba (0.5, 0.3) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.3,1.0,1.0,1.0,1.0}},
{136,"relative probe rgba (0.7, 0.4) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.7,0.4,1.0,1.0,1.0,1.0}},
{137,"relative probe rgba (0.7, 0.6) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.7,0.6,1.0,1.0,1.0,1.0}},
{138,"relative probe rgba (0.5, 0.7) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.7,1.0,1.0,1.0,1.0}},
{139,"relative probe rgba (0.3, 0.6) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.3,0.6,1.0,1.0,1.0,1.0}},
{142,"relative probe rgba (0.5, 0.2) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.2,1.0,1.0,1.0,1.0}},
{143,"relative probe rgba (0.8, 0.3) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.8,0.3,1.0,1.0,1.0,1.0}},
{144,"relative probe rgba (0.8, 0.7) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.8,0.7,1.0,1.0,1.0,1.0}},
{145,"relative probe rgba (0.5, 0.8) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.8,1.0,1.0,1.0,1.0}},
{146,"relative probe rgba (0.2, 0.7) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.2,0.7,0.0,0.0,0.0,0.0}},
{147,"relative probe rgba (0.2, 0.3) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.2,0.3,0.0,0.0,0.0,0.0}},
{26,"clear color 0.0 0.0 0.0 0.0",SR_CLEARCOLOR,0,NULL,{0.0,0.0,0.0,0.0}},
{27,"clear",SR_CLEAR,0,NULL,{0}},
{28,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{29,"clip plane 0 0 1 0 0.5",SR_PLANE,0,NULL,{0,0,1,0,0.5}},
{30,"clip plane 1 -1 1 0 0.8",SR_PLANE,0,NULL,{1,-1,1,0,0.8}},
{31,"clip plane 2 -1 -1 0 0.8",SR_PLANE,0,NULL,{2,-1,-1,0,0.8}},
{32,"clip plane 3 0 -1 0 0.5",SR_PLANE,0,NULL,{3,0,-1,0,0.5}},
{33,"clip plane 4 1 -1 0 0.8",SR_PLANE,0,NULL,{4,1,-1,0,0.8}},
{34,"clip plane 5 1 1 0 0.8",SR_PLANE,0,NULL,{5,1,1,0,0.8}},
{35,"enable GL_CLIP_PLANE0",SR_ENABLE,GL_CLIP_PLANE0,NULL,{0}},
{36,"enable GL_CLIP_PLANE1",SR_ENABLE,GL_CLIP_PLANE1,NULL,{0}},
{37,"enable GL_CLIP_PLANE2",SR_ENABLE,GL_CLIP_PLANE2,NULL,{0}},
{38,"enable GL_CLIP_PLANE3",SR_ENABLE,GL_CLIP_PLANE3,NULL,{0}},
{39,"enable GL_CLIP_PLANE4",SR_ENABLE,GL_CLIP_PLANE4,NULL,{0}},
{40,"enable GL_CLIP_PLANE5",SR_ENABLE,GL_CLIP_PLANE5,NULL,{0}},
{41,"draw rect 0.1 0.1 0.8 0.8",SR_QUAD,0,NULL,{0.1,0.1,0.8,0.8}},
{44,"relative probe rgba (0.3, 0.4) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.3,0.4,1.0,1.0,1.0,1.0}},
{45,"relative probe rgba (0.5, 0.3) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.3,1.0,1.0,1.0,1.0}},
{46,"relative probe rgba (0.7, 0.4) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.7,0.4,1.0,1.0,1.0,1.0}},
{47,"relative probe rgba (0.7, 0.6) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.7,0.6,1.0,1.0,1.0,1.0}},
{48,"relative probe rgba (0.5, 0.7) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.7,1.0,1.0,1.0,1.0}},
{49,"relative probe rgba (0.3, 0.6) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.3,0.6,1.0,1.0,1.0,1.0}},
{52,"relative probe rgba (0.2, 0.3) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.2,0.3,0.0,0.0,0.0,0.0}},
{53,"relative probe rgba (0.5, 0.2) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.5,0.2,0.0,0.0,0.0,0.0}},
{54,"relative probe rgba (0.8, 0.3) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.8,0.3,0.0,0.0,0.0,0.0}},
{55,"relative probe rgba (0.8, 0.7) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.8,0.7,0.0,0.0,0.0,0.0}},
{56,"relative probe rgba (0.5, 0.8) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.5,0.8,0.0,0.0,0.0,0.0}},
{57,"relative probe rgba (0.2, 0.7) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.2,0.7,0.0,0.0,0.0,0.0}},
{38,"clear color 0.0 0.0 0.0 0.0",SR_CLEARCOLOR,0,NULL,{0.0,0.0,0.0,0.0}},
{39,"clear",SR_CLEAR,0,NULL,{0}},
{40,"ortho 0 1 0 1",SR_ORTHO,0,NULL,{0,1,0,1}},
{41,"clip plane 0 0 1 0 -2.5",SR_PLANE,0,NULL,{0,0,1,0,-2.5}},
{42,"clip plane 1 -1 1 0 4",SR_PLANE,0,NULL,{1,-1,1,0,4}},
{43,"clip plane 2 -1 -1 0 14",SR_PLANE,0,NULL,{2,-1,-1,0,14}},
{44,"clip plane 3 0 -1 0 7.5",SR_PLANE,0,NULL,{3,0,-1,0,7.5}},
{45,"clip plane 4 1 -1 0 4",SR_PLANE,0,NULL,{4,1,-1,0,4}},
{46,"clip plane 5 1 1 0 -6",SR_PLANE,0,NULL,{5,1,1,0,-6}},
{47,"enable GL_CLIP_PLANE0",SR_ENABLE,GL_CLIP_PLANE0,NULL,{0}},
{48,"enable GL_CLIP_PLANE1",SR_ENABLE,GL_CLIP_PLANE1,NULL,{0}},
{49,"enable GL_CLIP_PLANE2",SR_ENABLE,GL_CLIP_PLANE2,NULL,{0}},
{50,"enable GL_CLIP_PLANE3",SR_ENABLE,GL_CLIP_PLANE3,NULL,{0}},
{51,"enable GL_CLIP_PLANE4",SR_ENABLE,GL_CLIP_PLANE4,NULL,{0}},
{52,"enable GL_CLIP_PLANE5",SR_ENABLE,GL_CLIP_PLANE5,NULL,{0}},
{53,"draw rect 0.1 0.1 0.8 0.8",SR_QUAD,0,NULL,{0.1,0.1,0.8,0.8}},
{56,"relative probe rgba (0.3, 0.4) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.3,0.4,1.0,1.0,1.0,1.0}},
{57,"relative probe rgba (0.5, 0.3) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.3,1.0,1.0,1.0,1.0}},
{58,"relative probe rgba (0.7, 0.4) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.7,0.4,1.0,1.0,1.0,1.0}},
{59,"relative probe rgba (0.7, 0.6) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.7,0.6,1.0,1.0,1.0,1.0}},
{60,"relative probe rgba (0.5, 0.7) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.5,0.7,1.0,1.0,1.0,1.0}},
{61,"relative probe rgba (0.3, 0.6) (1.0, 1.0, 1.0, 1.0)",SR_REL4,0,NULL,{0.3,0.6,1.0,1.0,1.0,1.0}},
{64,"relative probe rgba (0.2, 0.3) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.2,0.3,0.0,0.0,0.0,0.0}},
{65,"relative probe rgba (0.5, 0.2) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.5,0.2,0.0,0.0,0.0,0.0}},
{66,"relative probe rgba (0.8, 0.3) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.8,0.3,0.0,0.0,0.0,0.0}},
{67,"relative probe rgba (0.8, 0.7) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.8,0.7,0.0,0.0,0.0,0.0}},
{68,"relative probe rgba (0.5, 0.8) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.5,0.8,0.0,0.0,0.0,0.0}},
{69,"relative probe rgba (0.2, 0.7) (0.0, 0.0, 0.0, 0.0)",SR_REL4,0,NULL,{0.2,0.7,0.0,0.0,0.0,0.0}},
{36,"clear color 0.0 0.0 0.0 0.0",SR_CLEARCOLOR,0,NULL,{0.0,0.0,0.0,0.0}},
{37,"clear",SR_CLEAR,0,NULL,{0}},
{39,"clip plane 0 1.0 0.0 0.0 0.0",SR_PLANE,0,NULL,{0,1.0,0.0,0.0,0.0}},
{40,"enable GL_CLIP_PLANE0",SR_ENABLE,GL_CLIP_PLANE0,NULL,{0}},
{42,"ortho",SR_ORTHOWINDOW,0,NULL,{0}},
{45,"uniform float u_clipdist -1",SR_U_FLOAT,0,"u_clipdist",{-1}},
{47,"uniform vec2 u_offset 0.5 0.5",SR_U_VEC2,0,"u_offset",{0.5,0.5}},
{48,"draw arrays GL_POINTS 0 1",SR_ARRAYS,GL_POINTS,NULL,{0,1}},
{49,"probe rect rgba ( 0, 0, 20, 20) (0, 0, 0, 0)",SR_BOX4,0,NULL,{0,0,20,20,0,0,0,0}},
{51,"uniform vec2 u_offset 20 0.5",SR_U_VEC2,0,"u_offset",{20,0.5}},
{52,"draw arrays GL_LINES 0 2",SR_ARRAYS,GL_LINES,NULL,{0,2}},
{53,"probe rect rgba (20, 0, 20, 20) (0, 0, 0, 0)",SR_BOX4,0,NULL,{20,0,20,20,0,0,0,0}},
{55,"uniform vec2 u_offset 40 0",SR_U_VEC2,0,"u_offset",{40,0}},
{56,"draw arrays GL_TRIANGLE_STRIP 0 4",SR_ARRAYS,GL_TRIANGLE_STRIP,NULL,{0,4}},
{57,"probe rect rgba (40, 0, 20, 20) (0, 0, 0, 0)",SR_BOX4,0,NULL,{40,0,20,20,0,0,0,0}},
{60,"uniform float u_clipdist 0",SR_U_FLOAT,0,"u_clipdist",{0}},
{62,"uniform vec2 u_offset 0.5 20.5",SR_U_VEC2,0,"u_offset",{0.5,20.5}},
{63,"draw arrays GL_POINTS 0 1",SR_ARRAYS,GL_POINTS,NULL,{0,1}},
{64,"probe rect rgba ( 0, 20, 1, 1) (1, 0, 0, 1)",SR_BOX4,0,NULL,{0,20,1,1,1,0,0,1}},
{66,"uniform vec2 u_offset 20 20.5",SR_U_VEC2,0,"u_offset",{20,20.5}},
{67,"draw arrays GL_LINES 0 2",SR_ARRAYS,GL_LINES,NULL,{0,2}},
{68,"probe rect rgba (20, 20, 10, 1) (1, 0, 0, 1)",SR_BOX4,0,NULL,{20,20,10,1,1,0,0,1}},
{70,"uniform vec2 u_offset 40 20",SR_U_VEC2,0,"u_offset",{40,20}},
{71,"draw arrays GL_TRIANGLE_STRIP 0 4",SR_ARRAYS,GL_TRIANGLE_STRIP,NULL,{0,4}},
{72,"probe rect rgba (40, 20, 10, 10) (1, 0, 0, 1)",SR_BOX4,0,NULL,{40,20,10,10,1,0,0,1}},
{98,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{99,"probe all rgba 1 1 1 1",SR_ALL4,0,NULL,{1,1,1,1}},
{28,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{29,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{35,"uniform int zero 0",SR_U_INT,0,"zero",{0}},
{36,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{37,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{35,"uniform int zero 0",SR_U_INT,0,"zero",{0}},
{36,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{37,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{20,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{21,"probe all rgba 0 1 0 1",SR_ALL4,0,NULL,{0,1,0,1}},
{33,"uniform int i 0",SR_U_INT,0,"i",{0}},
{34,"uniform ivec4 expected_value 100 200 300 400",SR_U_IVEC4,0,"expected_value",{100,200,300,400}},
{35,"draw rect -1 -1 2 1",SR_QUAD,0,NULL,{-1,-1,2,1}},
{36,"uniform int i 1",SR_U_INT,0,"i",{1}},
{37,"uniform ivec4 expected_value 500 600 700 800",SR_U_IVEC4,0,"expected_value",{500,600,700,800}},
{38,"draw rect -1 0 2 1",SR_QUAD,0,NULL,{-1,0,2,1}},
{39,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{44,"uniform int i 0",SR_U_INT,0,"i",{0}},
{45,"uniform ivec2 expected_value 200 300",SR_U_IVEC2,0,"expected_value",{200,300}},
{46,"draw rect -1 -1 1 1",SR_QUAD,0,NULL,{-1,-1,1,1}},
{47,"uniform int i 1",SR_U_INT,0,"i",{1}},
{48,"uniform ivec2 expected_value 500 600",SR_U_IVEC2,0,"expected_value",{500,600}},
{49,"draw rect 0 -1 1 1",SR_QUAD,0,NULL,{0,-1,1,1}},
{50,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{51,"uniform int i 0",SR_U_INT,0,"i",{0}},
{52,"uniform ivec2 expected_value 800 900",SR_U_IVEC2,0,"expected_value",{800,900}},
{53,"draw rect -1 0 1 1",SR_QUAD,0,NULL,{-1,0,1,1}},
{54,"uniform int i 1",SR_U_INT,0,"i",{1}},
{55,"uniform ivec2 expected_value 1100 1200",SR_U_IVEC2,0,"expected_value",{1100,1200}},
{56,"draw rect 0 0 1 1",SR_QUAD,0,NULL,{0,0,1,1}},
{57,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{38,"uniform int i 0",SR_U_INT,0,"i",{0}},
{39,"uniform ivec2 expected_value 200 300",SR_U_IVEC2,0,"expected_value",{200,300}},
{40,"draw rect -1 -1 2 1",SR_QUAD,0,NULL,{-1,-1,2,1}},
{41,"uniform int i 1",SR_U_INT,0,"i",{1}},
{42,"uniform ivec2 expected_value 500 600",SR_U_IVEC2,0,"expected_value",{500,600}},
{43,"draw rect -1 0 2 1",SR_QUAD,0,NULL,{-1,0,2,1}},
{44,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{32,"uniform int i 0",SR_U_INT,0,"i",{0}},
{33,"uniform int expected_value 100",SR_U_INT,0,"expected_value",{100}},
{34,"draw rect -1 -1 2 1",SR_QUAD,0,NULL,{-1,-1,2,1}},
{35,"uniform int i 1",SR_U_INT,0,"i",{1}},
{36,"uniform int expected_value 200",SR_U_INT,0,"expected_value",{200}},
{37,"draw rect -1 0 2 1",SR_QUAD,0,NULL,{-1,0,2,1}},
{38,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{54,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{55,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{15,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{16,"probe all rgb 0.0 1.0 0.0",SR_ALL3,0,NULL,{0.0,1.0,0.0}},
{15,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{16,"probe all rgb 1.0 0.0 1.0",SR_ALL3,0,NULL,{1.0,0.0,1.0}},
{33,"uniform int zero 0",SR_U_INT,0,"zero",{0}},
{34,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{35,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{37,"uniform int zero 0",SR_U_INT,0,"zero",{0}},
{38,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{39,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{45,"uniform int zero 0",SR_U_INT,0,"zero",{0}},
{46,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{47,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{61,"uniform int zero 0",SR_U_INT,0,"zero",{0}},
{62,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{63,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{77,"uniform int zero 0",SR_U_INT,0,"zero",{0}},
{78,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{79,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{93,"uniform int zero 0",SR_U_INT,0,"zero",{0}},
{94,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{95,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{157,"uniform int zero 0",SR_U_INT,0,"zero",{0}},
{158,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{159,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{285,"uniform int zero 0",SR_U_INT,0,"zero",{0}},
{286,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{287,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{42,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{43,"probe all rgba 0 1 0 1",SR_ALL4,0,NULL,{0,1,0,1}},
{47,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{48,"probe all rgba 0 1 0 1",SR_ALL4,0,NULL,{0,1,0,1}},
{34,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{35,"probe all rgb 0. 1. 0.",SR_ALL3,0,NULL,{0.,1.,0.}},
{34,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{35,"probe all rgba 0. 1. 0. 0.",SR_ALL4,0,NULL,{0.,1.,0.,0.}},
{33,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{34,"probe all rgb 0. 1. 0.",SR_ALL3,0,NULL,{0.,1.,0.}},
{33,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{34,"probe all rgb 0. 1. 0.",SR_ALL3,0,NULL,{0.,1.,0.}},
{35,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{36,"probe all rgba 1. 0. 0. 0.",SR_ALL4,0,NULL,{1.,0.,0.,0.}},
{34,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{35,"probe all rgba 0. 1. 0. 0.",SR_ALL4,0,NULL,{0.,1.,0.,0.}},
{34,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{35,"probe all rgb 0. 1. 0.",SR_ALL3,0,NULL,{0.,1.,0.}},
{34,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{35,"probe all rgb 0. 1. 0.",SR_ALL3,0,NULL,{0.,1.,0.}},
{32,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{33,"probe all rgb 0. 1. 0.",SR_ALL3,0,NULL,{0.,1.,0.}},
{32,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{33,"probe all rgb 0. 1. 0.",SR_ALL3,0,NULL,{0.,1.,0.}},
{45,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{46,"probe all rgba 0.25 0.2 0.75 0.5",SR_ALL4,0,NULL,{0.25,0.2,0.75,0.5}},
{40,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{41,"probe all rgba 0.25 0.5 0.0 0.0",SR_ALL4,0,NULL,{0.25,0.5,0.0,0.0}},
{47,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{48,"probe all rgba 0.25 0.25 0.25 0.25",SR_ALL4,0,NULL,{0.25,0.25,0.25,0.25}},
{39,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{40,"probe all rgba 0.25 0.25 0.25 0.25",SR_ALL4,0,NULL,{0.25,0.25,0.25,0.25}},
{47,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{48,"probe all rgb 0 1 0",SR_ALL3,0,NULL,{0,1,0}},
{42,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{43,"probe all rgb 0 1 0",SR_ALL3,0,NULL,{0,1,0}},
{47,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{48,"probe all rgb 0 1 0",SR_ALL3,0,NULL,{0,1,0}},
{41,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{42,"probe all rgb 0 1 0",SR_ALL3,0,NULL,{0,1,0}},
{31,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{32,"probe all rgb 0 1 0",SR_ALL3,0,NULL,{0,1,0}},
{31,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{32,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{27,"uniform int i 0",SR_U_INT,0,"i",{0}},
{28,"uniform int expected_value 100",SR_U_INT,0,"expected_value",{100}},
{29,"draw rect -1 -1 2 1",SR_QUAD,0,NULL,{-1,-1,2,1}},
{30,"uniform int i 1",SR_U_INT,0,"i",{1}},
{31,"uniform int expected_value 200",SR_U_INT,0,"expected_value",{200}},
{32,"draw rect -1 0 2 1",SR_QUAD,0,NULL,{-1,0,2,1}},
{33,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{49,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{50,"probe all rgb 0 1 0",SR_ALL3,0,NULL,{0,1,0}},
{38,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{39,"probe all rgba 0.0 1.0 0.0 1.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,1.0}},
{22,"link error",SR_LINKERR,0,NULL,{0}},
{22,"link success",SR_LINKOK,0,NULL,{0}},
{33,"link error",SR_LINKERR,0,NULL,{0}},
{33,"link error",SR_LINKERR,0,NULL,{0}},
{33,"link success",SR_LINKOK,0,NULL,{0}},
{33,"link success",SR_LINKOK,0,NULL,{0}},
{22,"link error",SR_LINKERR,0,NULL,{0}},
{25,"link success",SR_LINKOK,0,NULL,{0}},
{43,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{44,"probe all rgba 0.0 1.0 0.0 0.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,0.0}},
{43,"draw rect -1 -1 2 2",SR_QUAD,0,NULL,{-1,-1,2,2}},
{44,"probe all rgba 0.0 1.0 0.0 0.0",SR_ALL4,0,NULL,{0.0,1.0,0.0,0.0}},
};
static const SrCase sr_cases[]={
{"sr-1aac3dbf4b70","tests/spec/arb_fragment_program/dph.shader_test","225b91886793d6e2de037dc88bcde0fb42c41c7a4355f05c83f5bad90f6d0ac2","GL_ARB_fragment_program",0,0,250,250,0,0,0,2,0,8},
{"sr-7fa7652e8948","tests/spec/arb_fragment_program/fdo30337a.shader_test","20711b32fdd6b78b46c1b3425becadb088774e0fbc69efba1dd85f9e8ea7af20","GL_ARB_vertex_program GL_ARB_fragment_program",0,13,250,250,0,0,2,2,8,5},
{"sr-86736601eb80","tests/spec/arb_fragment_program/fdo30337b.shader_test","498a7f734eaa06f5d245198f0dbce9836539d752fda5f77a78b9f11ed91761fe","GL_ARB_vertex_program GL_ARB_fragment_program",0,13,250,250,0,0,4,2,13,5},
{"sr-6207e12d6603","tests/spec/arb_fragment_program/fdo38145.shader_test","2600e435c8c6adc6dd0a1cef363d375948e17873dfb0bebac7160a1dd7aa11ba","GL_ARB_vertex_program GL_ARB_fragment_program",0,13,250,250,0,0,6,2,18,5},
{"sr-2ef9ed91330b","tests/spec/arb_fragment_program/fp-cmp.shader_test","71f42f8b9712ceb35e6682ba80b02e44226d8c728a9c03dc06f3ddf8540d03e0","GL_ARB_vertex_program GL_ARB_fragment_program",0,13,250,250,0,0,8,2,23,5},
{"sr-5d3e7c35d6e5","tests/spec/arb_fragment_program/fp-dst-aliasing-1.shader_test","84794825da58e231539659aa37a0a26e62d2b991097b201440d03c0dbb346504","GL_ARB_vertex_program GL_ARB_fragment_program",0,13,250,250,0,0,10,2,28,7},
{"sr-534085d84aea","tests/spec/arb_fragment_program/fp-dst-aliasing-2.shader_test","6d42e759bd58414d8fa5e89f2454c63832f5c8e060ceed588b671b0846c7a132","GL_ARB_vertex_program GL_ARB_fragment_program",0,13,250,250,0,0,12,2,35,7},
{"sr-4ccd3e722a42","tests/spec/arb_fragment_program/fp-ex2-sat.shader_test","de1f58fd0e6cc79951d5867234c046b8d11025c388df62acd832718edb5b1219","GL_ARB_vertex_program GL_ARB_fragment_program",0,13,250,250,0,0,14,2,42,6},
{"sr-5ed0bed72d89","tests/spec/arb_fragment_program/fp-two-constants.shader_test","fea3029e772fff55bcc575ef4fb085168786a189ffcf62b4f9d2744ff30bbcc6","GL_ARB_vertex_program GL_ARB_fragment_program",0,13,250,250,0,0,16,2,48,5},
{"sr-91f501aadaf6","tests/spec/arb_fragment_program/kil-swizzle.shader_test","69512214f35f9326aad84d2c427244b1530e8ac6a1a8745153feacfb304553ae","GL_ARB_fragment_program",0,0,250,250,0,0,18,2,53,31},
{"sr-d4ab9eab0de9","tests/spec/arb_fragment_program/lrp_sat.shader_test","b867dd5d39d41644f889525922825a03fe7e5a018de1d9d594a621868d3e314f","GL_ARB_fragment_program",0,0,250,250,0,0,20,2,84,5},
{"sr-3a456fbedffc","tests/spec/arb_fragment_program/texturing/tex-2d.shader_test","7bebf50127d2705e68c990bc31c778e7a4b8273439df1ea78acc9bba1e86960a","GL_ARB_fragment_program",0,0,250,250,0,0,22,2,89,10},
{"sr-23fbc5d56723","tests/spec/arb_fragment_program/texturing/txb-2d.shader_test","f5b5504499d552e4b011964f14aeea7d564b6e0b47eb9d7cb41def9091c0777b","GL_ARB_fragment_program",0,0,250,250,0,0,24,2,99,19},
{"sr-5a7ea3125117","tests/spec/arb_fragment_program/texturing/txp-2d.shader_test","857c123fef71ca197c59513e5b565adf508110839d73ebcd231954535d38ec95","GL_ARB_fragment_program",0,0,250,250,0,0,26,2,118,10},
{"sr-2f99acb4b37e","tests/spec/arb_vertex_program/arl.shader_test","b5481a8cc8609eafe158838a073d9f292e9ce2c77e1056daf81ab963f4d81f0f","GL_ARB_vertex_program",0,13,250,250,0,0,28,1,128,8},
{"sr-ddd35e232032","tests/spec/arb_vertex_program/compute_position_from_texcoord.shader_test","adfbbea90114fab52f1127122378aedd4b5ce8b71636883df8ca6dc780a4882a","GL_ARB_vertex_program",0,13,250,250,0,0,29,1,136,4},
{"sr-229736a920a2","tests/spec/arb_vertex_program/dataflow-bug.shader_test","6e7062b4281474e5c3d6a0bc8e79d35528e7dd11ff38a1a26df3c6783de1e524","GL_ARB_vertex_program",0,13,250,250,0,0,30,1,140,10},
{"sr-7454d369bc9d","tests/spec/arb_vertex_program/divide_by_zero.shader_test","19cc511135db154439044c8cde2bc5facf89c206ffe229088b2e03610e4b7280","GL_ARB_vertex_program",0,13,250,250,0,0,31,1,150,3},
{"sr-df362284626a","tests/spec/arb_vertex_program/fogcoord-dp3.shader_test","c090f4545f611c923fa81a549cccc07120c41d1f2a4d77839fb4ac5c311df5db","GL_ARB_vertex_program GL_ARB_fragment_program",0,13,250,250,0,0,32,2,153,7},
{"sr-80ccdaebc326","tests/spec/arb_vertex_program/fogcoord-dp4.shader_test","9ab1843fc3cedfd2e3e0befa5b0324c135b17a3ab0000b268b414f239a7d0811","GL_ARB_vertex_program GL_ARB_fragment_program",0,13,250,250,0,0,34,2,160,7},
{"sr-e3825d6dc0fe","tests/spec/arb_vertex_program/fogcoord-dph.shader_test","f8a14512c5b5e5731bd61ad5837f33ae89ba87d4a7b7e12d26f829797027e1c0","GL_ARB_vertex_program GL_ARB_fragment_program",0,13,250,250,0,0,36,2,167,7},
{"sr-d3f49dfc91de","tests/spec/arb_vertex_program/infinity_and_nan.shader_test","bc8743574bb19eee9d3a9d08edfd84a5cc83103c1fa1aad554cdb2f604de9b0a","GL_ARB_vertex_program",0,13,250,250,0,0,38,1,174,4},
{"sr-63457b181b8a","tests/spec/arb_vertex_program/instructions/abs.shader_test","cd1059710c22c585291b88428cf659ce5ff768765f6726496636b0093504187d","GL_ARB_vertex_program",0,13,250,250,0,0,39,1,178,4},
{"sr-44b4dd0c23c8","tests/spec/arb_vertex_program/instructions/add.shader_test","7807626d2c9a755e5fc64b5aeed7ab4d35cee3b25abcdb49d290bc9f12506bc4","GL_ARB_vertex_program",0,13,250,250,0,0,40,1,182,5},
{"sr-e4077016b7a4","tests/spec/arb_vertex_program/instructions/arl.shader_test","e8303aaba21582022867039f7813aff32c1b921fc134bbbf06512ebfb22331c4","GL_ARB_vertex_program",0,13,250,250,0,0,41,1,187,3},
{"sr-f0c746fbeec7","tests/spec/arb_vertex_program/instructions/dp3.shader_test","88d9e5a49ce73db18fa85ffa369162747044de26465c4744021751dd90b6bdb4","GL_ARB_vertex_program",0,13,250,250,0,0,42,1,190,5},
{"sr-f58beb26a761","tests/spec/arb_vertex_program/instructions/dp4.shader_test","512707c08890e854f90b68204de1222ede5d196f1bfd45999949ac9dda0831fe","GL_ARB_vertex_program",0,13,250,250,0,0,43,1,195,5},
{"sr-e0b182aae7fc","tests/spec/arb_vertex_program/instructions/dph.shader_test","7cba21782fa50cb7dc6c2c247532b80d5759b670bea504c3c14d584dbee75757","GL_ARB_vertex_program",0,13,250,250,0,0,44,1,200,5},
{"sr-82ffaeace146","tests/spec/arb_vertex_program/instructions/dst.shader_test","c3643356a11ed425f9a5e9e07dd57519709068eac7cb34703ba5b591bbaf2eda","GL_ARB_vertex_program",0,13,250,250,0,0,45,1,205,3},
{"sr-d5872296ef36","tests/spec/arb_vertex_program/instructions/ex2.shader_test","e578577bb7156362748eabfa49836f72d910463459f63e01d2d882d1298edc8b","GL_ARB_vertex_program",0,13,250,250,0,0,46,1,208,3},
{"sr-76ecd638fc51","tests/spec/arb_vertex_program/instructions/exp.shader_test","4482e52e1df2ff6277a0489b70cc1cd9d09abfaa112a0fe4c92dea13a2a161fb","GL_ARB_vertex_program",0,13,250,250,0,0,47,1,211,3},
{"sr-a5e9131d5a23","tests/spec/arb_vertex_program/instructions/flr.shader_test","ffb7801e54638cc18605af5ab02b123e09e6cc4e85b6ac752c20aa4b23b89cda","GL_ARB_vertex_program",0,13,250,250,0,0,48,1,214,3},
{"sr-33c600a19ef6","tests/spec/arb_vertex_program/instructions/frc.shader_test","48a53f29141e8e5fbf1a5b734d67c186a1e1aee0888eecb65c4d76943afbedd9","GL_ARB_vertex_program",0,13,250,250,0,0,49,1,217,3},
{"sr-57acacbe1599","tests/spec/arb_vertex_program/instructions/lg2.shader_test","724bd5f1fc2a0dd4465381795c89197857e96a1ccf43194580b86e3c58effc4b","GL_ARB_vertex_program",0,13,250,250,0,0,50,1,220,3},
{"sr-9706a88802e4","tests/spec/arb_vertex_program/instructions/lit_degenerate_case.shader_test","03c275607459974118eef94b8683a73c4b95e0cecd7709ef2163c978ea221e0b","GL_ARB_vertex_program",0,13,250,250,0,0,51,1,223,3},
{"sr-fe60fdbe7b17","tests/spec/arb_vertex_program/instructions/lit_x_lt_0.shader_test","ed9f50151f71046c2de357331acafdbabade495abbf1c8c630fe65a83f8ab189","GL_ARB_vertex_program",0,13,250,250,0,0,52,1,226,3},
{"sr-9d83d0bc32d4","tests/spec/arb_vertex_program/instructions/log.shader_test","622fb18ffb5ecf1cd75b921e57b2c11f1113033f36465c9f76fa1ebf27357e46","GL_ARB_vertex_program",0,13,250,250,0,0,53,1,229,3},
{"sr-aa6810ee2682","tests/spec/arb_vertex_program/instructions/mad.shader_test","476e14bcbb2f529fd9c1b769a2790374447a6a9ba5593a1936beb4c3cadaa7ad","GL_ARB_vertex_program",0,13,250,250,0,0,54,1,232,6},
{"sr-03de1c9ac58b","tests/spec/arb_vertex_program/instructions/max.shader_test","88e551e66e43b32d2445ae29226f58c07bd3a95bbeaf1099ea35e4038f6bb0cf","GL_ARB_vertex_program",0,13,250,250,0,0,55,1,238,5},
{"sr-6a435d6e1415","tests/spec/arb_vertex_program/instructions/min.shader_test","508e153d3bcc8655e540e3cdb0afcfa68826eba4256048e6bce9435b18e421d5","GL_ARB_vertex_program",0,13,250,250,0,0,56,1,243,5},
{"sr-915b41e63262","tests/spec/arb_vertex_program/instructions/mov_with_swizzle.shader_test","8d947efaf968325b58f8cebbf351fd9aeda1377ada143b1e03d96d75210c4dcc","GL_ARB_vertex_program",0,13,250,250,0,0,57,1,248,4},
{"sr-86a220cd88af","tests/spec/arb_vertex_program/instructions/mul_with_swizzle_and_masking.shader_test","5200093aa2808cace0fc82f9d0774a14e966bf5ef363cdcabe572f798228ed5b","GL_ARB_vertex_program",0,13,250,250,0,0,58,1,252,5},
{"sr-e91d1a046c82","tests/spec/arb_vertex_program/instructions/pow_exponentiation.shader_test","2295ba6f62b63b9ad0228476525357be021097f60fbde8c53ba427bd245f0c37","GL_ARB_vertex_program",0,13,250,250,0,0,59,1,257,3},
{"sr-488bb5515c75","tests/spec/arb_vertex_program/instructions/rcp_reciprocal.shader_test","d51bf6799d18ccba83ff110da0699998d522b6c8a379d9f26c3920f0f56f0007","GL_ARB_vertex_program",0,13,250,250,0,0,60,1,260,3},
{"sr-43f90b3d0a81","tests/spec/arb_vertex_program/instructions/rsq_reciprocal_square_root.shader_test","fcd1a16d626c4327bdb98ad9b66f20b5e7dd03daa9137d13caf7116b4deb9a9f","GL_ARB_vertex_program",0,13,250,250,0,0,61,1,263,3},
{"sr-bf2771dcaeb7","tests/spec/arb_vertex_program/instructions/rsq_reciprocal_square_root_of_negative_value.shader_test","83cb3b4a6b80789fa287e2d68f090f17695b7d46e822c052ee48a864f4d59f48","GL_ARB_vertex_program",0,13,250,250,0,0,62,1,266,3},
{"sr-d96f5b6af9bc","tests/spec/arb_vertex_program/instructions/sge.shader_test","d4acd0a8326510685ca53dcb817352e65e1ea15a17f5696a2749244320a0f089","GL_ARB_vertex_program",0,13,250,250,0,0,63,1,269,5},
{"sr-075a0939a42c","tests/spec/arb_vertex_program/instructions/slt.shader_test","039d361cdeaa6568101cd157082bf9c6d4363647d0a838003aaef7dee3bcee6f","GL_ARB_vertex_program",0,13,250,250,0,0,64,1,274,5},
{"sr-ac3176030c66","tests/spec/arb_vertex_program/instructions/sub_with_swizzle.shader_test","f4af5d4a1792d3a3f42522b7067cd1f0ae5b24cc80979073204de6a330b2d069","GL_ARB_vertex_program",0,13,250,250,0,0,65,1,279,5},
{"sr-e4b24768ecdb","tests/spec/arb_vertex_program/instructions/swz.shader_test","9dac86353a21fdf69f25f635b0e39e33b9c0e6079a2657439aee6618443180f4","GL_ARB_vertex_program",0,13,250,250,0,0,66,1,284,4},
{"sr-f4af14e1c95f","tests/spec/arb_vertex_program/instructions/swz_constant.shader_test","c3ac5b9373fd591e870c267c4449c95cdcd7783cc6ad353f0e16b0ef93a99086","GL_ARB_vertex_program",0,13,250,250,0,0,67,1,288,4},
{"sr-3fe5b53dedf8","tests/spec/arb_vertex_program/instructions/swz_negative.shader_test","9da1574ddad90ecf6323e57c5a918ef76fa025be4d92f8273956582a8da8d4ac","GL_ARB_vertex_program",0,13,250,250,0,0,68,1,292,4},
{"sr-b8c82591f404","tests/spec/arb_vertex_program/instructions/swz_src_and_constants.shader_test","6f40481600e01d20a3542f901dc66cddf1fbe77da56674c22d4f6215ff8a261f","GL_ARB_vertex_program",0,13,250,250,0,0,69,1,296,4},
{"sr-c510c8e1a57b","tests/spec/arb_vertex_program/instructions/swz_src_negative_and_constants.shader_test","f021c6f8129e7a8753ac627b6cb3c5e58bf40ea9fb9d2ef3a53be59ab40e8594","GL_ARB_vertex_program",0,13,250,250,0,0,70,1,300,4},
{"sr-b7caf1bacf36","tests/spec/arb_vertex_program/instructions/xpd.shader_test","fcc1c23a6596baed45fb97e4eb1432f3646af00a9e1b4be8af12e09a0b19601f","GL_ARB_vertex_program",0,13,250,250,0,0,71,1,304,5},
{"sr-007452980540","tests/spec/arb_vertex_program/instructions/xpd_same_src_and_dst_arg.shader_test","9d9b3ec8a948ed1eff6cce207b07d662083116b7c7a62474607490ff101a3822","GL_ARB_vertex_program",0,13,250,250,0,0,72,1,309,5},
{"sr-3c4026feabf0","tests/spec/arb_vertex_program/vp-arl-constant-array-huge-offset-neg.shader_test","c0d90fec931cb5f399c9805709a18031513436810515859332378d0f5be135c7","GL_ARB_vertex_program",0,13,250,250,0,0,73,1,314,21},
{"sr-33c8e41244d4","tests/spec/arb_vertex_program/vp-arl-constant-array-huge-offset.shader_test","1de0b412749bbb9ee41a8779c69c3d4e7731947df00bba8cb0d1b26b5d3f1685","GL_ARB_vertex_program",0,13,250,250,0,0,74,1,335,21},
{"sr-69afbf12e669","tests/spec/arb_vertex_program/vp-arl-constant-array-huge-overwritten.shader_test","d85b769869f3499ebe4b9dfe73b26a6bb50059eef8928b8176921cb80bc8b5de","GL_ARB_vertex_program",0,13,250,250,0,0,75,1,356,6},
{"sr-10f1093f51bb","tests/spec/arb_vertex_program/vp-arl-constant-array-huge-relative-offset.shader_test","145415923316c605e8a184a5a85f5a7fab2d6e60a5b73d0a0aa9cfff5aba4503","GL_ARB_vertex_program",0,13,250,250,0,0,76,1,362,21},
{"sr-30199ade2f0c","tests/spec/arb_vertex_program/vp-arl-constant-array-huge-varying.shader_test","e07a1ffdc0ba5ae0d62cdbbe2357f9360f10917e1e557436b85a5a6712df50ab","GL_ARB_vertex_program",0,13,250,250,0,0,77,1,383,5},
{"sr-bdcf7870b521","tests/spec/arb_vertex_program/vp-arl-constant-array-huge.shader_test","b8d6403a8219ab09b5ea3e61df3d66422f92d973fe177ebc352f320b0ab167b8","GL_ARB_vertex_program",0,13,250,250,0,0,78,1,388,21},
{"sr-ecb986f3012d","tests/spec/arb_vertex_program/vp-arl-constant-array-varying.shader_test","b12f3ff34ac858e2d9b9fc0ea528878e99272c0e30eaf59a3b4aa340355c362f","GL_ARB_vertex_program",0,13,250,250,0,0,79,1,409,5},
{"sr-1d789048e0b2","tests/spec/arb_vertex_program/vp-arl-constant-array.shader_test","25b9fcbd1be0099cf390cee707f8b1a53ac69cd8edbd7fbbdbd2a9e056f74c12","GL_ARB_vertex_program",0,13,250,250,0,0,80,1,414,6},
{"sr-38cb5148f0e4","tests/spec/arb_vertex_program/vp-arl-env-array.shader_test","de4f9ecc64545d43e7a69ab48baceb4c50346e82fe3091251b1764aeb8c78541","GL_ARB_vertex_program",0,13,250,250,0,0,81,1,420,10},
{"sr-d824432d9ff4","tests/spec/arb_vertex_program/vp-arl-local-array.shader_test","57aa5d6649a003a28852050f45ffd379a6943f7ea733fd5517b002dc56b66ccb","GL_ARB_vertex_program",0,13,250,250,0,0,82,1,430,10},
{"sr-49454985a138","tests/spec/arb_vertex_program/vp-arl-neg-array-2.shader_test","9ff233f911759ffca1bffeeb60523b9b5e7848922043b9f10262ab8cd0615b90","GL_ARB_vertex_program",0,13,250,250,0,0,83,1,440,6},
{"sr-af7242bc3c55","tests/spec/arb_vertex_program/vp-arl-neg-array.shader_test","a2c4c99ddb167eab787fcf93d421997c497ba34d22910ec5abb0334bc9818b89","GL_ARB_vertex_program",0,13,250,250,0,0,84,1,446,6},
{"sr-700d530fb885","tests/spec/arb_vertex_program/vp-constant-array-huge.shader_test","ef5412a0773d9f361c4f5bd5bdcb2acd0add2464d3c600fd19934a63cf437137","GL_ARB_vertex_program",0,13,250,250,0,0,85,1,452,6},
{"sr-00138584fadc","tests/spec/arb_vertex_program/vp-constant-array.shader_test","b3f7347406c0b2287ef74ddb9b59a330f08294306bf3f437c38aae281d45b938","GL_ARB_vertex_program",0,13,250,250,0,0,86,1,458,5},
{"sr-28fffdd59f2f","tests/spec/arb_vertex_program/vp-constant-negate.shader_test","cdd473d674cf909263ec10242e75f482467e1fbedcbaf4d6fe86ccd38575a6d8","GL_ARB_vertex_program",0,13,250,250,0,0,87,1,463,5},
{"sr-282d4df84be6","tests/spec/arb_vertex_program/vp-exp-alias.shader_test","efa3254ded31e35887a40382036fbf85767591d73be9119ee5cc0529ebbee448","GL_ARB_vertex_program",0,13,250,250,0,0,88,1,468,5},
{"sr-860578baf3c2","tests/spec/arb_vertex_program/vp-max.shader_test","c777bc2d49fd531f08654656e172ac30607ccc456fac57f8ac83ddaffae1377f","GL_ARB_vertex_program",0,13,250,250,0,0,89,1,473,5},
{"sr-3d908b849f47","tests/spec/arb_vertex_program/vp-min.shader_test","a69a92d9bd26d85f321d6fd8858dfd65486679443af5124ce71a97967a693bf0","GL_ARB_vertex_program",0,13,250,250,0,0,90,1,478,5},
{"sr-eb9187dcad9f","tests/spec/arb_vertex_program/vp-sge-alias.shader_test","91435aeb3f63b5d26f3ccb4f45c10c5e15c49b6f1faee47f13eba030c499a171","GL_ARB_vertex_program",0,13,250,250,0,0,91,1,483,5},
{"sr-de9341134e0d","tests/spec/arb_vertex_program/vp-two-constants.shader_test","fa5d8058ef96f49329860549209ea95c99cffd48573c2acbd389f606c453c187","GL_ARB_vertex_program",0,13,250,250,0,0,92,1,488,5},
{"sr-3a46093e13a1","tests/spec/arb_vertex_program/z-write.shader_test","144ca2bd8154ef97b935c209896979a084614835c4acc28120106f8d913af87f","GL_ARB_vertex_program",0,13,250,250,1,0,93,1,493,9},
{"sr-1b4b347e4be6","tests/spec/glsl-1.10/execution/and-operator-short-circuit.shader_test","d2f004f125ca63de67dc856bc344273537a46abf0b0b557d3135359e359abaac","",110,0,250,250,0,0,94,1,502,2},
{"sr-ca51f2686fe4","tests/spec/glsl-1.10/execution/built-in-constants.shader_test","7e53f36f9ca1deb583a73042e001def931bbec150f65a109acd9f1be6ea0af25","",110,0,250,250,0,0,95,2,504,2},
{"sr-a028c668c808","tests/spec/glsl-1.10/execution/chained-assignment.shader_test","87ef3e3bc4d1fe49780c11d44567d55e8cdc3ee4c01369611648269dd15c5ac3","",110,0,250,250,0,0,97,1,506,2},
{"sr-f94f45ae212f","tests/spec/glsl-1.10/execution/fs-bool-less-compare-false.shader_test","54ad2253547d5d77635493d48c80de1bcc713bcf4ac1d7e5d3dd3932b6a9f903","",110,0,250,250,0,0,98,2,508,5},
{"sr-a904284d69e2","tests/spec/glsl-1.10/execution/fs-bool-less-compare-true.shader_test","cc75326a2f486b8ade32a767408bc6a2f4f41fa592b5b9d0cef028a0c891f218","",110,0,250,250,0,0,100,2,513,5},
{"sr-aeb0c906da46","tests/spec/glsl-1.10/execution/fs-dfdx-accuracy.shader_test","52bfdaa78af4e9b6a48b34d1cfdcc95b836b2a591e7b7914f39a9301b615b992","",110,0,250,250,0,0,102,2,518,3},
{"sr-0a716f710f24","tests/spec/glsl-1.10/execution/fs-dfdy-accuracy.shader_test","f25dd317055ce00d25a1ceb049a51d0be604b96a92a4529f7e6d6ad91cf7cd45","",110,0,250,250,0,0,104,2,521,3},
{"sr-94b56320a4f0","tests/spec/glsl-1.10/execution/fs-discard-deep-branch.shader_test","195086ddd9ef5f1f0c8db787c46af53bbafb2254669bff381c0849f3053f9548","",110,0,20,20,0,0,106,2,524,12},
{"sr-688e77c4fb42","tests/spec/glsl-1.10/execution/fs-equal-of-expression.shader_test","ca6d3a62994aaa70a8f592cf7db4314a4e29b872909cf34dee63b7be8c2dcf38","",110,0,250,250,0,0,108,2,536,4},
{"sr-8f8b970ce9c4","tests/spec/glsl-1.10/execution/fs-frontfacing-ternary-0.0-neg-1.0.shader_test","8096acdea7b877be4f65de17da6a541a32408d5fcdd5b3fe1e4dd9e427f36a24","",110,0,250,250,0,0,110,2,540,6},
{"sr-f96c12513e5b","tests/spec/glsl-1.10/execution/fs-frontfacing-ternary-1-neg-1.shader_test","833756b597ac822da6eec92bdf5eaeaedc3f2215b08a530fc323aa7d28ab2db5","",110,0,250,250,0,0,112,2,546,8},
{"sr-82c759ae09e6","tests/spec/glsl-1.10/execution/fs-frontfacing-ternary-1.0-neg-1.0.shader_test","962442ba33b12b830fe8b4e7f738324119908a142db31f3354a0f061f0afcb01","",110,0,250,250,0,0,114,2,554,6},
{"sr-347e5dcb330b","tests/spec/glsl-1.10/execution/fs-frontfacing-ternary-neg-1.0-1.0.shader_test","fd03aa614932b2b2f1e1224de8553c7bd4a6521a0158c4e50474afb41c98bad3","",110,0,250,250,0,0,116,2,560,6},
{"sr-e99b4e496e11","tests/spec/glsl-1.10/execution/fs-frontfacing-ternary-vec4-neg-1.0-1.0.shader_test","187486ffd72a88c728ef0583022704f6e7d867866f8a5aa93903c2dd4de98ccc","",110,0,250,250,0,0,118,2,566,2},
{"sr-a07e16b33458","tests/spec/glsl-1.10/execution/fs-loop-bounds-unrolled.shader_test","79ef29114e323934a1d213fd022c77ed7302d66cd64176ec53d2eae17b790c61","",110,0,250,250,0,0,120,2,568,2},
{"sr-306412d722e0","tests/spec/glsl-1.10/execution/fs-loop-return.shader_test","b23d36ff8db760951735eff7ccbbc6695c21ae1c283368e54796b4fb74843e21","",110,0,250,250,0,0,122,2,570,2},
{"sr-38fe3edb51a9","tests/spec/glsl-1.10/execution/fs-max-max-max.shader_test","02bb3f251c9bd288894db5559f9b84acefc9e1104ea401bf84237b169fb7c454","",110,0,250,250,0,0,124,2,572,6},
{"sr-730b2a5b3936","tests/spec/glsl-1.10/execution/fs-min-min-min.shader_test","ff60a9648a42b79413880ff03052de73f4958f14868dcf05309f630adbd4cfbb","",110,0,250,250,0,0,126,2,578,6},
{"sr-7a1917df9393","tests/spec/glsl-1.10/execution/fs-nested-loop-unrolled-with-return.shader_test","2557d3fb9ad6cca83f2f4038577ce74cd426d69f2a4e4e39854906f819b3e358","",110,0,250,250,0,0,128,2,584,3},
{"sr-c893eb427e9f","tests/spec/glsl-1.10/execution/fs-nested-return-in-loop-nested_in_if.shader_test","fc9ef0777ad7c32cabfdeb58657bf44f46f4dab72666d99cd6f0a59a7dcf7267","",110,0,250,250,0,0,130,2,587,4},
{"sr-98d3c9c124fa","tests/spec/glsl-1.10/execution/fs-notEqual-inline-function.shader_test","6f7ab79dbfe84fd870fbeb9c39a3de1ae7b4e01a5cedd0726c441050301a419d","",110,0,250,250,0,0,132,2,591,2},
{"sr-bff498b6156f","tests/spec/glsl-1.10/execution/fs-notEqual-of-expression.shader_test","60089edf4cde8c368360e353034d6101971ea427f306d83c2d5fb4fc974a1da2","",110,0,250,250,0,0,134,2,593,4},
{"sr-1141c124664d","tests/spec/glsl-1.10/execution/fs-op-assign-mult-ivec2-ivec2-overwrite.shader_test","10abf2bffc9bb86ac9fa3240cf5c74f117a346c237fdec72ed482490517411a9","",110,0,250,250,0,0,136,2,597,5},
{"sr-efcd295268fd","tests/spec/glsl-1.10/execution/fs-saturate-exp2.shader_test","dd8f3a87b2e606619e9955034586590b71defbc9150553ac163e969ace51e45d","",110,0,250,250,0,0,138,2,602,3},
{"sr-e788d50764b1","tests/spec/glsl-1.10/execution/fs-saturate-pow.shader_test","0270a63b171681865958e8318b2232bd8813534c2067df14c647801663c1df62","",110,0,250,250,0,0,140,2,605,4},
{"sr-e2ba5cc3fcfe","tests/spec/glsl-1.10/execution/fs-saturate-sqrt.shader_test","d4bc4ee05e775126f37a4dc55464e94b1d2eadcb88e70ff47787a5c1e33f5ec9","",110,0,250,250,0,0,142,2,609,3},
{"sr-b28c571dcf16","tests/spec/glsl-1.10/execution/fs-step.shader_test","646f6a2b137ef66956e896051b20654c626a5a893486b341a375c3372572757e","",110,0,250,250,0,0,144,2,612,12},
{"sr-f34ae07d71ef","tests/spec/glsl-1.10/execution/fs-texture-select.shader_test","a332b3a83bbc5509808329c21418da338f0e17a49bf0e0c1e15b4fb888f035dd","",110,0,250,250,0,0,146,2,624,6},
{"sr-7e8f204bcc83","tests/spec/glsl-1.10/execution/fs-vector-indexing-kills-all-channels.shader_test","1ebb2eda89c7ddf7efdbb6775c2928a7cad8de00ef25cae8744ee03c01a1eee0","",110,0,250,250,0,0,148,2,630,4},
{"sr-0feb1bdc1c51","tests/spec/glsl-1.10/execution/function-calls/glsl-function-chain16-in-array-indirect.shader_test","05d2493922cd360947521ea71dc5fe92dd57765ae40f4edfe9671e5ab556e8b8","",110,0,250,250,0,0,150,5,634,3},
{"sr-00fa4afbd5c9","tests/spec/glsl-1.10/execution/function-calls/glsl-function-chain16-in-array-indirect2.shader_test","c1e3ca45f90a8baf51de5d282e24ac5389ad4eca813a311cd5dd79b315337c0b","",110,0,250,250,0,0,155,5,637,3},
{"sr-82ef447a8462","tests/spec/glsl-1.10/execution/function-calls/glsl-function-chain16-in-array.shader_test","fba9726490462aab5b8cb3adc9fde8a86bfff29caa96dd49f0498bc444fc1fc4","",110,0,250,250,0,0,160,5,640,2},
{"sr-cf3d00a2bb23","tests/spec/glsl-1.10/execution/function-calls/glsl-function-chain16-in-array2.shader_test","08fe9c9f9c3ce1e703700a86d1394ccbaf284519966ca47b83a19867c3f86bf7","",110,0,250,250,0,0,165,5,642,2},
{"sr-3b63e7eac937","tests/spec/glsl-1.10/execution/function-calls/glsl-function-chain16-in-swizzle.shader_test","8a49fc490bf9a79f8123420fd6ccebb687afac275fe426450018990393620eae","",110,0,250,250,0,0,170,5,644,2},
{"sr-163fc558aabc","tests/spec/glsl-1.10/execution/function-calls/glsl-function-chain16-in.shader_test","6f27a9528167225ea338c4bb90db17e386bc03073d3ead35672238f7401de6b3","",110,0,250,250,0,0,175,5,646,2},
{"sr-e244613574d7","tests/spec/glsl-1.10/execution/function-calls/glsl-function-chain16-inout-swizzle.shader_test","adf9c91e190cd8bf573549946b26e185d659932810894c764a2388bb2c979749","",110,0,250,250,0,0,180,5,648,2},
{"sr-3b508b0a6b8d","tests/spec/glsl-1.10/execution/function-calls/glsl-function-chain16-inout.shader_test","40e0820740f02f5f6cd1a322f4d61f54c44a5b5141fa7b9a174c618265e57fe9","",110,0,250,250,0,0,185,5,650,2},
{"sr-7ee533ccef73","tests/spec/glsl-1.10/execution/function-calls/glsl-function-chain16-out-swizzle.shader_test","46d8992efbf90e89cb40daed75d6acba578d9137fb726b97c6200e7c2ed88b38","",110,0,250,250,0,0,190,5,652,2},
{"sr-24823405b3a6","tests/spec/glsl-1.10/execution/function-calls/glsl-function-chain16-out.shader_test","ee05dc4506b51992b152dd242afec6ab52cdadcf974ac36256968a8c37f7e3d5","",110,0,250,250,0,0,195,5,654,2},
{"sr-2865bb6340aa","tests/spec/glsl-1.10/execution/global-vars-and-initializers-expression.shader_test","e51ed566abd47a07287e498f04a9f3996ebdbc3c0f6151bef9d69829a140eacd","",110,0,250,250,0,0,200,1,656,2},
{"sr-2874174d3258","tests/spec/glsl-1.10/execution/global-vars-and-initializers.shader_test","d4963cba53b24e99b8b4de1a49b257fd5d742dacd3d2fb17f45d7ce87fb219fc","",110,0,250,250,0,0,201,1,658,2},
{"sr-4909cc5198c2","tests/spec/glsl-1.10/execution/matrix-vector-multiply-lhs.shader_test","09d0d278307d48538065909fcb085130ec91dbb51b1dd81f0419f7fa9e4cf490","",110,0,250,250,0,0,202,1,660,3},
{"sr-f6448c103535","tests/spec/glsl-1.10/execution/matrix-vector-multiply-rhs.shader_test","31134395ac8736cc26ed484305b40f05e768f1390c1aaafcfce15a38bd4bfddb","",110,0,250,250,0,0,203,1,663,3},
{"sr-97a168f347b0","tests/spec/glsl-1.10/execution/matrix-vector-multiply-scale.shader_test","394793690a5277ef79c9bf456211b5b5eebb47b93a1f22c395b53342a0935630","",110,0,250,250,0,0,204,1,666,3},
{"sr-db4178005968","tests/spec/glsl-1.10/execution/nested-function-calls-same-function.shader_test","ba23f2420dd0026931b1a293caa3e89154c39deb939d64cf644a1cfdebf550c5","",110,0,250,250,0,0,205,1,669,2},
{"sr-0c9945cc1136","tests/spec/glsl-1.10/execution/nested-function-calls.shader_test","c7eac760ff9eec5c2a77b1f052af80de4ba926cd56681de36ab37ef9facfae15","",110,0,250,250,0,0,206,1,671,2},
{"sr-81c08bfb9d0b","tests/spec/glsl-1.10/execution/or-operator-short-circuit.shader_test","962dedfe29426a08589ce05e7cc56ff81c1be692f6bd6d88a04dde9979298c25","",110,0,250,250,0,0,207,1,673,2},
{"sr-940db86670af","tests/spec/glsl-1.10/execution/samplers/glsl-fs-shadow1D-01.shader_test","f7d983272e651786ef3cb3fb194274db3324b93e78179dddafb7c86f6eb21bb6","",110,0,250,250,0,0,208,2,675,9},
{"sr-a4fa3a28002f","tests/spec/glsl-1.10/execution/samplers/glsl-fs-shadow1D-02.shader_test","83e84d685646ba5bcd06b84a83ce951614fadfdf0d520ba0bda734f56f8b0d20","",110,0,250,250,0,0,210,2,684,9},
{"sr-c70a24a1685b","tests/spec/glsl-1.10/execution/samplers/glsl-fs-shadow1D-03.shader_test","90b2f42f8c47fedd0e2da327261ea42aa8235939e5233e97efb8aa8bdd4f3dca","",110,0,250,250,0,0,212,2,693,9},
{"sr-27d805789486","tests/spec/glsl-1.10/execution/samplers/glsl-fs-shadow1D-04.shader_test","534ece339bf4e17e288f0020fa328c2f5aaea68b18b81134866a8eec5532857f","",110,0,250,250,0,0,214,2,702,9},
{"sr-d36ac6d99afd","tests/spec/glsl-1.10/execution/samplers/glsl-fs-shadow1D-05.shader_test","75e48c56c854e41b4dea5bb7072cd20c318c45b91534bebe9a17672b3ad9f85f","",110,0,250,250,0,0,216,2,711,9},
{"sr-28e6586ca49d","tests/spec/glsl-1.10/execution/samplers/glsl-fs-shadow1D-06.shader_test","f270876f83f89f3508ff9767853cbc08df560f74b37fa96b9a1e20707fbd476f","",110,0,250,250,0,0,218,2,720,9},
{"sr-474406cc6150","tests/spec/glsl-1.10/execution/samplers/glsl-fs-shadow1D-07.shader_test","017951ea95fbc29620989222c5f113695c50ab037b2383e7e1b10a1e88a5a559","",110,0,250,250,0,0,220,2,729,9},
{"sr-e5515c1efe77","tests/spec/glsl-1.10/execution/samplers/glsl-fs-shadow1D-08.shader_test","115da362f87d06858b91d09526f794654399e7ebb99caf3fe5a62fd13a43f4b2","",110,0,250,250,0,0,222,2,738,9},
{"sr-2c807830a345","tests/spec/glsl-1.10/execution/samplers/glsl-fs-shadow1D-09.shader_test","7db2f3d26dd01ca563f163070563a15289cd47f0e5fda237c0058d260cc34439","",110,0,250,250,0,0,224,2,747,9},
{"sr-0b824096e3cf","tests/spec/glsl-1.10/execution/samplers/glsl-fs-shadow1D-bias.shader_test","a826a6be22e3a4a16e50f2b1178ee354e01d09c4a06f83b0fcee235d12ce4269","",110,0,250,250,0,0,226,2,756,9},
{"sr-591816a5c84d","tests/spec/glsl-1.10/execution/samplers/glsl-fs-shadow1D.shader_test","aa322e983baf12ab6bb01ab89eae36a4609e094c8aaa621415d564e90b5c18fd","",110,0,250,250,0,0,228,2,765,9},
{"sr-40fed7f7955e","tests/spec/glsl-1.10/execution/samplers/glsl-fs-shadow1DProj-bias.shader_test","3bb04d758cc80b6b5886346dc7529dc1f437244866ded0d78c98ee37e77b51f1","",110,0,250,250,0,0,230,2,774,9},
{"sr-ec5be1e96da4","tests/spec/glsl-1.10/execution/samplers/glsl-fs-shadow1DProj.shader_test","d0487b02ea167df638e3fa1a51265e24488f0cac99022a53d217560aaf9b1445","",110,0,250,250,0,0,232,2,783,9},
{"sr-9d7f0262c296","tests/spec/glsl-1.10/execution/samplers/glsl-fs-shadow2D-01.shader_test","ac03405d0401013daf2ee299bcc2645eef0d991c2665dd619258f7b9ccc1d3e1","",110,0,250,250,0,0,234,2,792,9},
{"sr-dd6353f615a2","tests/spec/glsl-1.10/execution/samplers/glsl-fs-shadow2D-02.shader_test","0e28e414033c6cf2dfb5fb4a9860b99fee2a1949e8863d49c41fff27d9040ce0","",110,0,250,250,0,0,236,2,801,9},
{"sr-f0533cbb2a2c","tests/spec/glsl-1.10/execution/samplers/glsl-fs-shadow2D-03.shader_test","84a0278ff1e4a0327ea528a6c93b717a28f59c30ac2f5817be611057f82447bd","",110,0,250,250,0,0,238,2,810,9},
{"sr-5a77f1004249","tests/spec/glsl-1.10/execution/samplers/glsl-fs-shadow2D-04.shader_test","c9db797e008aa7a7b70116c0f8dcfd0905c8cc2e7858b14808b516c563025a8d","",110,0,250,250,0,0,240,2,819,9},
{"sr-a503090e33ee","tests/spec/glsl-1.10/execution/samplers/glsl-fs-shadow2D-05.shader_test","9e81e429bcb322ee5576dc81ca2001ae19bcea56928427f04a46aefa249d3f8e","",110,0,250,250,0,0,242,2,828,9},
{"sr-20beff41dd2b","tests/spec/glsl-1.10/execution/samplers/glsl-fs-shadow2D-06.shader_test","d8951d80807c8735eb708d83a21308d07ef411c8b9b8d0c08a6ea2fc99ec53be","",110,0,250,250,0,0,244,2,837,9},
{"sr-15aa7adfd82d","tests/spec/glsl-1.10/execution/samplers/glsl-fs-shadow2D-07.shader_test","4435ec7314f6daf06cab9c0cc4cd7a6f59fc5822cecf5d8bb2b1f7aa14d7ec1a","",110,0,250,250,0,0,246,2,846,9},
{"sr-cea6bb12b2b1","tests/spec/glsl-1.10/execution/samplers/glsl-fs-shadow2D-08.shader_test","0dd0057156ffcf77ceca0ef2b6b2dfd3ae5150ca22eb0bf7d01ed6db7bdd2482","",110,0,250,250,0,0,248,2,855,9},
{"sr-bd071370bbf6","tests/spec/glsl-1.10/execution/samplers/glsl-fs-shadow2D-09.shader_test","b2811a6112e35ff8e0512cacdf3d51ad392df8ad98b37947baee78e31f0f11cf","",110,0,250,250,0,0,250,2,864,9},
{"sr-35e26dde37d3","tests/spec/glsl-1.10/execution/samplers/glsl-fs-shadow2D-bias.shader_test","dd934b581c53367b6e956ac71954803cbe2f6de4a2b573efd207c2e75bd90ec1","",110,0,250,250,0,0,252,2,873,9},
{"sr-efe12d86f34b","tests/spec/glsl-1.10/execution/samplers/glsl-fs-shadow2D-clamp-z.shader_test","a298e1b3951a93124787eab7ef6eab3fe69990d293e7d6b6b65ec4f395d15c39","",110,0,250,250,0,0,254,2,882,9},
{"sr-a033988ab418","tests/spec/glsl-1.10/execution/samplers/glsl-fs-shadow2D.shader_test","2288a231f0c78f69cb316b7246498fb54452b30e588fa043974ac8523b160bba","",110,0,250,250,0,0,256,2,891,9},
{"sr-8f4fae769a38","tests/spec/glsl-1.10/execution/samplers/glsl-fs-shadow2DProj-bias.shader_test","d1704f592307786e2e05a1d6a31587d8f5764faea21562d565aca5d059ab6f79","",110,0,250,250,0,0,258,2,900,9},
{"sr-b66e46b19879","tests/spec/glsl-1.10/execution/samplers/glsl-fs-shadow2DProj.shader_test","e4690684ec80f6906efb3c5c9d2e60adb91f679c8057c812e84f9553664d3ef9","",110,0,250,250,0,0,260,2,909,9},
{"sr-36419f7d73e5","tests/spec/glsl-1.10/execution/samplers/in-parameter-array.shader_test","fb6d63ad47497fd29328191f55d858999edbd7cd0fd1080bb76b213089628eb2","",110,0,250,250,0,0,262,2,918,9},
{"sr-ad736afac5fb","tests/spec/glsl-1.10/execution/samplers/in-parameter-nested-struct.shader_test","2f49a9c1d00efe665b16f284ae974260834555e2960f8cada45a8e0dc67d5514","",110,0,250,250,0,0,264,2,927,9},
{"sr-cb541ad041eb","tests/spec/glsl-1.10/execution/samplers/in-parameter-struct-array.shader_test","b2a69df9a56a9fee09d72c933557b4c60f69cb9efe86f5d50728963f426e1f7b","",110,0,250,250,0,0,266,2,936,8},
{"sr-d669c02c2af9","tests/spec/glsl-1.10/execution/samplers/in-parameter-struct.shader_test","a5a01ce94b16329029c498e42c46ed2eee5b144694e64369f98105701d551c6f","",110,0,250,250,0,0,268,2,944,8},
{"sr-938ecef50110","tests/spec/glsl-1.10/execution/samplers/in-parameter.shader_test","f6885466d34ee79ca68cfc59e8ccc39db69e73b55cca303ae44f445f8dc419c2","",110,0,250,250,0,0,270,2,952,8},
{"sr-7b719c1dc94b","tests/spec/glsl-1.10/execution/samplers/normal-parameter-struct.shader_test","833749913d574b9dd4c5c3a6de0bc1ccfa469c713eed5fd8e9d93bc57ac2200f","",110,0,250,250,0,0,272,2,960,8},
{"sr-e62d497469d3","tests/spec/glsl-1.10/execution/samplers/normal-parameter.shader_test","ba95e7c0f934a12d4cd831b8dbeb84a853a92c3ddc2aaf75af45cb70a8278a06","",110,0,250,250,0,0,274,2,968,8},
{"sr-7235852ff596","tests/spec/glsl-1.10/execution/sequence-operator.shader_test","d804d0c9b08a2f793da3dac7d3a68550592d4217e8a3efc0df8775df081c4726","",110,0,250,250,0,0,276,1,976,2},
{"sr-df9c6378c09d","tests/spec/glsl-1.10/execution/struct-array.shader_test","67002b4c82adec063791401facb8d2aab0d4072aa9aab8087c2540f807842d21","",110,0,250,250,0,0,277,1,978,2},
{"sr-9b184c1815d1","tests/spec/glsl-1.10/execution/struct-multiple-instances.shader_test","8e9d4f04db15cd5ea78a84ff4a41f3f74df92a368427ef166904ac110efa51b7","",110,0,250,250,0,0,278,1,980,2},
{"sr-65e69c4c391c","tests/spec/glsl-1.10/execution/struct-nested.shader_test","2726c42244b343a81f5c171df21647ef380d819060e274e531db6dcd4968ee5f","",110,0,250,250,0,0,279,1,982,2},
{"sr-a4f323970f04","tests/spec/glsl-1.10/execution/struct.shader_test","c9d527f237107a45d14cc156293ac7e0a1bf750a6e056823bba37457455ff7d8","",110,0,250,250,0,0,280,1,984,2},
{"sr-a59b1f8a997d","tests/spec/glsl-1.10/execution/swizzle-rgba.shader_test","757e5e3608f03534e8edd4c91266eb65bae024ebc2fac28d2c938a34e375c487","",110,0,250,250,0,0,281,1,986,2},
{"sr-9bbd351dd63f","tests/spec/glsl-1.10/execution/swizzle-stpq.shader_test","12e4ce7badc3cf9f82aa58b40a6b579fd81a4485034b55025d1b67f310c20abf","",110,0,250,250,0,0,282,1,988,2},
{"sr-4c09ff1eb399","tests/spec/glsl-1.10/execution/swizzle.shader_test","96ffceb9ef7fde6a59a43c5e4682d0fc197c837baad05ba606a60bef487bff77","",110,0,250,250,0,0,283,1,990,2},
{"sr-e91aa19cba87","tests/spec/glsl-1.10/execution/swizzled-expression.shader_test","ea62ba435b41a5f8440f6e698c081985d2ae7326fa81e72607b4156ab186806a","",110,0,250,250,0,0,284,1,992,2},
{"sr-143750790945","tests/spec/glsl-1.10/execution/swizzled-swizzled-swizzle.shader_test","c901abb49611151e5afe72a731f468f76c58344647c56eaa80cb9d83b864f664","",110,0,250,250,0,0,285,1,994,2},
{"sr-2a348a1edacf","tests/spec/glsl-1.10/execution/swizzled-writemask-rgba.shader_test","a18291e0bc4d423b5c459495c11d479c2ad29d07e7ea5f2084acc32c75d98211","",110,0,250,250,0,0,286,1,996,2},
{"sr-c6e177925399","tests/spec/glsl-1.10/execution/swizzled-writemask-split.shader_test","95c12e7417e0102368548764f42f8e166e56fc796efccd0842618d6ac5cb1ea8","",110,0,250,250,0,0,287,1,998,2},
{"sr-8906ba7d51bf","tests/spec/glsl-1.10/execution/swizzled-writemask-stpq.shader_test","359dcc248c0b988860de3aee6ad6ec949faf73851451e2774b24741b3f3e2fb1","",110,0,250,250,0,0,288,1,1000,2},
{"sr-f6a6c0c4d1c9","tests/spec/glsl-1.10/execution/swizzled-writemask.shader_test","5a15e280e3b8d6d8b2f98dabd210cd4fc525fc97d51c72bd746e138ba4581b6c","",110,0,250,250,0,0,289,1,1002,2},
{"sr-5dcbf3d1e35f","tests/spec/glsl-1.10/execution/temp_arrays.shader_test","714e3eedc3b29289c7da46c6c587eeaa6ad4cd6c4db5ad035266b5118602d9ba","",110,0,250,250,0,0,290,2,1004,7},
{"sr-668db9d145aa","tests/spec/glsl-1.10/execution/texture1D.shader_test","7ee5c41b5b477b819b805dc5ae93a92f5d7ee405b964513e7b377b4b06543969","",110,0,250,250,0,0,292,1,1011,7},
{"sr-ff0e8ed5b142","tests/spec/glsl-1.10/execution/texture2D-computed-coordinate.shader_test","f33c6586efea795aa0e2931caddd86c05183ac7ce66cf14f2a49f4c6b305ea92","",110,0,250,250,0,0,293,1,1018,4},
{"sr-f8a7b45d5956","tests/spec/glsl-1.10/execution/texture3D-computed-coord.shader_test","73f840d78e43684db109a3b073104124ec367c3377d88bc621b912b13c3f597a","",110,0,250,250,0,0,294,1,1022,7},
{"sr-16f86927da3a","tests/spec/glsl-1.10/execution/texture3D.shader_test","481ef70f49c3f8e1a948d9a1da017cb508ca70a6cd7a160ef4f9d5922387e20e","",110,0,250,250,0,0,295,1,1029,7},
{"sr-318d4229392b","tests/spec/glsl-1.10/execution/uniform-matrix.shader_test","371024877991f2bf62183bf268a3c3f172ced1b63da13bc51718f3dc82a4344d","",110,0,250,250,0,0,296,1,1036,3},
{"sr-be985e7d7aaf","tests/spec/glsl-1.10/execution/uniform-update.shader_test","92892d245b7766b99992fe16a8c5ae6c6801d416fd8cd80dfcb4a409f69b1e8e","",110,0,250,250,0,0,297,2,1039,18},
{"sr-7f61bd092541","tests/spec/glsl-1.10/execution/variable-indexing/fs-input-array-float-index-rd.shader_test","0fb73bf41d6c3ddcbdd39f69c2eb5ff2170c8659425a0e1a7e256d66e6aec91c","",110,0,250,250,0,0,299,2,1057,35},
{"sr-0be4cc1adb10","tests/spec/glsl-1.10/execution/variable-indexing/fs-input-array-vec2-index-rd.shader_test","640b24fbada8aa1b85a8a9a9df676e673e7846a4423e75fb6358a257f9181f4a","",110,0,250,250,0,0,301,2,1092,35},
{"sr-6a640d160b20","tests/spec/glsl-1.10/execution/variable-indexing/fs-input-array-vec3-index-rd.shader_test","886315190e2c616555736e0ffa0d57aadbcfe11f76653d56809247be317d1b77","",110,0,250,250,0,0,303,2,1127,35},
{"sr-e7f6648062f1","tests/spec/glsl-1.10/execution/variable-indexing/fs-input-array-vec4-index-rd.shader_test","4a414aaa24ddff14d26aae4176f8f2b71e0b07ec820847a3b3106d8521def217","",110,0,250,250,0,0,305,2,1162,35},
{"sr-e731d1c12106","tests/spec/glsl-1.10/execution/variable-indexing/vs-output-array-float-index-wr.shader_test","192c482fd2290ad05f589e3eb01e254e701ae62570bba58c5c0d532c425d926b","",110,0,250,250,0,0,307,2,1197,27},
{"sr-2b5411af6405","tests/spec/glsl-1.10/execution/variable-indexing/vs-output-array-vec2-index-wr.shader_test","9b69147947354db6e6fdad7bd8c4ab232eba91f5a82c89caa90db9c6daa771c6","",110,0,250,250,0,0,309,2,1224,27},
{"sr-57055e3f5771","tests/spec/glsl-1.10/execution/variable-indexing/vs-output-array-vec3-index-wr.shader_test","85336bf17b8f80fad9fb1d42bcec1f96e6ba702fba90a3f4cdb0189a9e9203f4","",110,0,250,250,0,0,311,2,1251,27},
{"sr-52d7e93a616c","tests/spec/glsl-1.10/execution/variable-indexing/vs-output-array-vec4-index-wr.shader_test","5bd1fee86f2718a7a027fe792542707eef6db16bf789f1923ac1e8816c25da96","",110,0,250,250,0,0,313,2,1278,27},
{"sr-920d3e432739","tests/spec/glsl-1.10/execution/void-ternary.shader_test","3c7ae200dcc5fef81dc6727d68d1da10aaf5193c65538745b545f60c017823e6","",110,0,250,250,0,0,315,2,1305,3},
{"sr-559f449418ca","tests/spec/glsl-1.10/execution/vs-call-in-nested-loop.shader_test","b63cd0bade8ee6f264c955225b2021567e5459973d0b038cdd68d3dd1bf178ae","",110,0,250,250,0,0,317,2,1308,2},
{"sr-32b440dbe863","tests/spec/glsl-1.10/execution/vs-equal-of-expression.shader_test","784ebe5b3b781cea1259834455bbbe2d1accfa6a7503191cd02d5f4d3ff81b4f","",110,0,250,250,0,0,319,2,1310,3},
{"sr-a5cce02292d4","tests/spec/glsl-1.10/execution/vs-float-main-return-at-end.shader_test","3b47d9b9aada1135beab1c4a66cd1fa9e05073af154c337a7dc4eebaab85b316","",110,0,250,250,0,0,321,2,1313,6},
{"sr-a472dc5a7347","tests/spec/glsl-1.10/execution/vs-float-main-return.shader_test","fe82cd0018e6baaf5dad0c71c218f074efb1ac4145f67524ee5cfffcfacac9cf","",110,0,250,250,0,0,323,2,1319,6},
{"sr-584e419ad494","tests/spec/glsl-1.10/execution/vs-inner-loop-counts-outer-loop-var.shader_test","9df686fd2f4d6c1cd983a4b3f649ae4553e2466813234711b53494c1a3f0e0af","",110,0,250,250,0,0,325,2,1325,2},
{"sr-fa9456711577","tests/spec/glsl-1.10/execution/vs-inner-loop-modifies-outer-loop-var.shader_test","cdac2c31e47691bae87181ec860550fdc30fdffa239090ecc5641c94c68f8e76","",110,0,250,250,0,0,327,2,1327,2},
{"sr-c35c55e3f38c","tests/spec/glsl-1.10/execution/vs-integer-reduction.shader_test","4268741b809a9af23850c1194beb11cf770ceab78a4d135645a1165b8fbfcdbf","",110,0,250,250,0,0,329,2,1329,3},
{"sr-b2fd550162d5","tests/spec/glsl-1.10/execution/vs-loop-bounds-unrolled.shader_test","0042944bf832e91e17bbcea2b0410149d04f252b49f2cad63f7fc2790330017b","",110,0,250,250,0,0,331,2,1332,2},
{"sr-f589a4c3460b","tests/spec/glsl-1.10/execution/vs-loop-complex-unroll-cf-before-terminators.shader_test","8fb1b6719f2a5bfa9e4d6629a0e93460ba10f82340bcce94d95b8a3c6b20309e","",110,0,250,250,0,0,333,2,1334,7},
{"sr-ea521dc9a344","tests/spec/glsl-1.10/execution/vs-loop-complex-unroll-cf-inside-terminator.shader_test","9e1584a623be940b1340bf176a4fd646a7ac0e8606bdad0be433e7655cd6cd07","",110,0,250,250,0,0,335,2,1341,7},
{"sr-2d8a97bf6107","tests/spec/glsl-1.10/execution/vs-loop-complex-unroll-with-else-break.shader_test","4a90d06f144cb92a8a9d1aec463b4f655112116443f134cc11b760ded362f9a6","",110,0,250,250,0,0,337,2,1348,16},
{"sr-7e47ad042fc6","tests/spec/glsl-1.10/execution/vs-loop-complex-unroll.shader_test","4bfb7178a93cb27f6342ce961e596d496e1b00e3f3288a13e677fc5c313f2ded","",110,0,250,250,0,0,339,2,1364,16},
{"sr-b49a6dd953cb","tests/spec/glsl-1.10/execution/vs-loop-simple-unroll-cf-inside-terminator.shader_test","6ff71ee62a4728c4a0d16cb23f674eb96006db3c8e142eba665013628ca12d02","",110,0,250,250,0,0,341,2,1380,3},
{"sr-da03aef2c9c7","tests/spec/glsl-1.10/execution/vs-loop-unroll-else-break-unreachable-then-break.shader_test","31e4502fe9e3365de349f2d71eb74e21dff0544f13d9a1ca79438af3f9d44c22","",110,0,250,250,0,0,343,2,1383,3},
{"sr-1b7fd480d3f1","tests/spec/glsl-1.10/execution/vs-loop-zero-iterations-two-exits.shader_test","4a58c6e81ad2bd75bc0298ff39c3f3e6c82695cf5e44c49dd711aa2eb2f8a438","",110,0,250,250,0,0,345,2,1386,7},
{"sr-781961ee158b","tests/spec/glsl-1.10/execution/vs-loop-zero-iterations-two-exits2.shader_test","09a3e2457201053e6a043e40b832973380f32c7fd7d0def59530b140c8f63ce3","",110,0,250,250,0,0,347,2,1393,7},
{"sr-a44088320e41","tests/spec/glsl-1.10/execution/vs-mat2-array-assignment.shader_test","18d4326155b216d8d675e1102c420e4efc4e9adc99e601c86140d3dd5e4ba51b","",110,0,250,250,0,0,349,2,1400,4},
{"sr-6b2850952ae8","tests/spec/glsl-1.10/execution/vs-mat2-struct-assignment.shader_test","aa4daa16acf491301fdb2f2adea8260b58bf5e5bbb2c03be58c5fa3f46930df8","",110,0,250,250,0,0,351,2,1404,4},
{"sr-016d5fd5b08c","tests/spec/glsl-1.10/execution/vs-nested-return-sibling-if.shader_test","c63d656d3fce31eab7d988cedcb0add7fb3d170853612e39352eb13a0c29bfe3","",110,0,250,250,0,0,353,2,1408,11},
{"sr-d104d1d0b60b","tests/spec/glsl-1.10/execution/vs-nested-return-sibling-if2.shader_test","65eabd89401ccd276049f73c2ae3dab57c6a0742017b2bfc2252de2d07bdda4f","",110,0,250,250,0,0,355,2,1419,11},
{"sr-f0b09b7f77be","tests/spec/glsl-1.10/execution/vs-nested-return-sibling-loop.shader_test","ff2bda5b434c77292e43368775d50d9cfd2f3644a3a1449c3696534576ad3fe3","",110,0,250,250,0,0,357,2,1430,11},
{"sr-38e001170a8a","tests/spec/glsl-1.10/execution/vs-nested-return-sibling-loop2.shader_test","6cb33a0a2853b5c2e68293e3d37bf4d75337ee1a674e41330627703b9613d22e","",110,0,250,250,0,0,359,2,1441,11},
{"sr-3c909ccea1da","tests/spec/glsl-1.10/execution/vs-notEqual-inline-function.shader_test","2e2f77ea1ebd933902870f2a1e5a22f298c33802f29645de81b3b85c2a1e1e62","",110,0,250,250,0,0,361,2,1452,2},
{"sr-32de5000996b","tests/spec/glsl-1.10/execution/vs-notEqual-of-expression.shader_test","2f9c3e18db917b2c1ee768a54718b69a12bb1807d9fb769a98512e061026bc52","",110,0,250,250,0,0,363,2,1454,3},
{"sr-a0530c6e5a0f","tests/spec/glsl-1.10/execution/vs-saturate-exp2.shader_test","8ff1633b8a44910a356d81693f2a636148d651b292dd7d4dcd32333f04f82729","",110,0,250,250,0,0,365,2,1457,3},
{"sr-8cfba6caeacb","tests/spec/glsl-1.10/execution/vs-saturate-pow.shader_test","e21f65ce2fcf8b98544d80709a8281148e225e70fd76338cbad8cba8a3a99408","",110,0,250,250,0,0,367,2,1460,4},
{"sr-deb5387e87a7","tests/spec/glsl-1.10/execution/vs-saturate-sqrt.shader_test","3c80bccf8ab6fcc646ee24a5883c044ea7d5770eef3a7687f3966826cfbc67bb","",110,0,250,250,0,0,369,2,1464,3},
{"sr-d1e2cf2ca4d3","tests/spec/glsl-1.10/execution/vs-swizzle-swizzle-lhs-2.shader_test","2475df9917fd84c17e90db6e01b1b0b65f094b2282af4070c9ecd108bc0e5161","",110,0,250,250,0,0,371,2,1467,2},
{"sr-41020a8530f6","tests/spec/glsl-1.10/execution/vs-swizzle-swizzle-lhs-3.shader_test","56538be1dbe2acde4a1867381b94f49364387f88ab454dadbe45e8607299b049","",110,0,250,250,0,0,373,2,1469,2},
{"sr-2948730bf60e","tests/spec/glsl-1.10/execution/vs-swizzle-swizzle-lhs.shader_test","1ac5eace91d116065d675b5ad32664a9b726819681481addd06c4e6fc2564a2c","",110,0,250,250,0,0,375,2,1471,2},
{"sr-ba1f61cfb5ad","tests/spec/glsl-1.10/execution/vs-swizzle-swizzle-rhs.shader_test","47e1f1a5c4b6c5abe2ec0781697793c04be57c6bd6c8cf4b71700c8a2119618b","",110,0,250,250,0,0,377,2,1473,2},
{"sr-f74dc54ea256","tests/spec/glsl-1.10/execution/vs-vec2-main-return.shader_test","4442056145a148ad58e55e783d439ce0d6e97c52e690b460b73eb35cd6d10366","",110,0,250,250,0,0,379,2,1475,6},
{"sr-fa1333719627","tests/spec/glsl-1.10/execution/vs-vec3-main-return.shader_test","cd1d46fe6d732a5d78724b1b30a5aa1be5ce07c98662a2ab79cb936f8c6eb3e6","",110,0,250,250,0,0,381,2,1481,6},
{"sr-97c3fa696145","tests/spec/glsl-1.10/execution/vs-vec4-main-return.shader_test","e11e4a0d79ac7852e50d9824bd988bb27f5a1d88d69fe36675e0ba2be66d8d5b","",110,0,250,250,0,0,383,2,1487,6},
{"sr-964b26393278","tests/spec/glsl-1.10/execution/vs-vector-indexing-kills-all-channels.shader_test","2da88884284055e98331960dae69e6fe746fb02e9421dc9961bd354734b74bf7","",110,0,250,250,0,0,385,2,1493,4},
{"sr-08bacc26d3c4","tests/spec/glsl-1.10/execution/vsfs-unused-array-member.shader_test","9f88a4dce0e1d8fe3998d8e973558ee7938f1bd0c0c77430a7dce90c6e7d2f0c","",110,0,250,250,0,0,387,2,1497,3},
{"sr-db2dfa128f34","tests/spec/glsl-1.10/execution/writemask.shader_test","e130e75fcd096af472904d636c1aca1cc9f8300ef5802ba5f36127c2d1a7618a","",110,0,250,250,0,0,389,1,1500,2},
{"sr-bf4362d13765","tests/spec/glsl-1.10/glsl-fs-discard-only.shader_test","6dc07c7dda459bba73d3474cc04944ab559e7937cb36002a07cec80b3a9f8ac2","",110,0,250,250,1,0,390,2,1502,8},
{"sr-efbe02d62e98","tests/spec/glsl-1.10/linker/access-builtin-global-from-fn-unknown-to-main.shader_test","e406ea7dc3c6b8463198fe9dcbd0e60d2f0af18842bb6a94baa708bfa6adbd91","",110,0,250,250,0,0,392,3,1510,2},
{"sr-674bc0a844fb","tests/spec/glsl-1.10/linker/gl_Position-not-written-check.shader_test","d83b1333c9721d01fb765301b18e7621cb49a2cfcc549b87467b17f8c888677a","",110,0,250,250,0,0,395,2,1512,1},
{"sr-753d57af2208","tests/spec/glsl-1.10/linker/global-const-intializer-mismatch.shader_test","c726551e492daf24387e42f14fb773584cd3a94f8e6a576ad7894a9defe3812a","",110,0,250,250,0,0,397,2,1513,1},
{"sr-a5ae14b07900","tests/spec/glsl-1.10/linker/global-initializer-matched-2-shaders.shader_test","1fc26fcd467de342b7ca5241800cf26ca39f2e91f8c40460d20110e4fc175723","",110,0,250,250,0,0,399,2,1514,1},
{"sr-e01a281411df","tests/spec/glsl-1.10/linker/global-initializer-matched-expression.shader_test","2735db561516286203cd1ce40f21b2ee02180f28848ccdecd5a719737476eddc","",110,0,250,250,0,0,401,2,1515,1},
{"sr-457f4cbe3df2","tests/spec/glsl-1.10/linker/global-initializer-mismatch-2-shaders.shader_test","17ff339861431bd63d198fb6906ee7f1aed10061419264d67127d22689984585","",110,0,250,250,0,0,403,2,1516,1},
{"sr-1b27bfde5254","tests/spec/glsl-1.10/linker/global-initializer-mismatch-3-shaders.shader_test","e06aa78efd3a147fb06b19a5d52f497644801ab5d587d2ef8f930cce35022f01","",110,0,250,250,0,0,405,3,1517,1},
{"sr-b0c8ddcbfac2","tests/spec/glsl-1.10/linker/global-initializer-unmatched-expression.shader_test","1daab5f84d3893ce09279ff4c0a89f286539c7a5a205f11b73a8042698969739","",110,0,250,250,0,0,408,2,1518,1},
{"sr-b1445516ebf3","tests/spec/glsl-1.10/linker/global-single-initializer-2-shaders.shader_test","da92ebf5ca2f2263ae8c8799eccf8da3f1b8dacac8d9f372441b4c51b7a8849b","",110,0,250,250,0,0,410,2,1519,1},
{"sr-885a2ca6ef64","tests/spec/glsl-1.10/linker/link-array-of-struct-of-array.shader_test","c16909280435b756dede5221567f604c665f3aa3961f08f88ccd371a883534ae","",110,0,250,250,0,0,412,1,1520,1},
{"sr-179b7bd44520","tests/spec/glsl-1.10/linker/override-builtin-const-01.shader_test","1f7b5c2a5b62fc967d2943ccba32ddb22a2e4ae4b2bd904589b8946443cf26ce","",110,0,250,250,0,0,413,2,1521,2},
{"sr-a41d67a44ea4","tests/spec/glsl-1.10/linker/override-builtin-const-02.shader_test","d307c59a1f55c75d716b6b58ad8ad5fbe2868bcab5b2d8eb8e83e56875fa9ccd","",110,0,250,250,0,0,415,2,1523,2},
{"sr-6863e36c300f","tests/spec/glsl-1.10/linker/override-builtin-const-03.shader_test","f0198ac2ce8ece714f8803435f027a0263eb0ec44dc1aae9b3b77a51dedc43c8","",110,0,250,250,0,0,417,3,1525,2},
{"sr-2bca0226b253","tests/spec/glsl-1.10/linker/override-builtin-const-04.shader_test","ab917d944e308a679db3f7d5e9251127ab5c793184a5256c868c2cf79c151dc5","",110,0,250,250,0,0,420,3,1527,2},
{"sr-e5a5ff304e0e","tests/spec/glsl-1.10/linker/override-builtin-const-05.shader_test","2473e127d7f1b63c0f2dcd02f8fcb0ea5798a41ad55956f6c74accbbcab4b82c","",110,0,250,250,0,0,423,2,1529,2},
{"sr-8dce7b6e4e30","tests/spec/glsl-1.10/linker/override-builtin-const-06.shader_test","14bb7e263883c38895a05f080f373ebab457a7b9bd3d1df5ca923bdc6ff21a31","",110,0,250,250,0,0,425,3,1531,2},
{"sr-43ab0d70b91b","tests/spec/glsl-1.10/linker/override-builtin-const-07.shader_test","6bec282c613b230101aa4bc945ec5e61da976cb2b6ff742734fe01dead9166da","",110,0,250,250,0,0,428,3,1533,2},
{"sr-cfff7ce9f801","tests/spec/glsl-1.10/linker/override-builtin-const-08.shader_test","c5c4edfca765537724cef9613894add57f4e52ad5f9f59c6595bd8b8cd6c2b8f","",110,0,250,250,0,0,431,3,1535,2},
{"sr-c9edf06d3f8b","tests/spec/glsl-1.10/linker/override-builtin-uniform-01.shader_test","abf2549271b2f2aede1f03a6622a86a96e1b58771d43f7815c4f4dd4525aceeb","",110,0,250,250,0,0,434,2,1537,3},
{"sr-5298e645bc34","tests/spec/glsl-1.10/linker/override-builtin-uniform-02.shader_test","ffc6c51e14c98b8ee83d1810fbf8cbe24c61a9c9a8f7d595c39eb30b307fe90b","",110,0,250,250,0,0,436,2,1540,3},
{"sr-b9114448b63c","tests/spec/glsl-1.10/linker/override-builtin-uniform-03.shader_test","e769e02f9e3287ff2f787de1e6fc4c03f925b08ac31a99ea9c62e804b88236b6","",110,0,250,250,0,0,438,3,1543,3},
{"sr-6371a6d21640","tests/spec/glsl-1.10/linker/override-builtin-uniform-04.shader_test","6497ce01d94e2c3b1e26d07b2118b23d1896d88f2974179efaac3fecce1fac74","",110,0,250,250,0,0,441,3,1546,3},
{"sr-a9b66fbd1ef1","tests/spec/glsl-1.10/linker/override-builtin-uniform-05.shader_test","19cfce2e855a91038422366cf1988ff8c5967477be8ff6670b9241a630ea5b0c","",110,0,250,250,0,0,444,2,1549,3},
{"sr-854667c82cfb","tests/spec/glsl-1.10/linker/override-builtin-uniform-06.shader_test","f7336f76555fcbf2c0a82719af8a58aec4a46a96584576e107f2c7aab93124d0","",110,0,250,250,0,0,446,3,1552,3},
{"sr-a0f665ea37b6","tests/spec/glsl-1.10/linker/override-builtin-uniform-07.shader_test","1a6aa49c381b168a9129da76fb338087ee6b7c8adc63fb72e954bce079aed392","",110,0,250,250,0,0,449,3,1555,3},
{"sr-9a3aa2aab299","tests/spec/glsl-1.10/linker/override-builtin-uniform-08.shader_test","94998dc3fa518a89b3ef72ddb988d0b0bb2a21f4ef3e0af9a2ab994749204d57","",110,0,250,250,0,0,452,3,1558,3},
{"sr-6f1f8b1b262e","tests/spec/glsl-1.10/linker/texcoord-varying.shader_test","cdcc8ebcd64d8b28f14e64250d51052e9636b683ab3daeade8d6d15559a68fd5","",110,0,250,250,0,0,455,2,1561,2},
{"sr-970a520b26d2","tests/spec/glsl-1.10/linker/uniform-type-mismatch.shader_test","b9dfe309625df66983827c0ae1c97d1d776d15b7e402d8935b60447cfd32d474","",110,0,250,250,0,0,457,2,1563,1},
{"sr-d4b656ab6e9d","tests/spec/glsl-1.10/linker/varying-read-but-not-written.shader_test","11edae01f3a82895a5cea3e305e6f512b58f5708615965c8842592e1d4127350","",110,0,250,250,0,0,459,2,1564,1},
{"sr-233296aa48b4","tests/spec/glsl-1.10/linker/varying-var-mismatch.shader_test","d7a1f1fde003658799e8c9069caf4926b454c46093bba2f821e7d572397ddfe3","",110,0,250,250,0,0,461,2,1565,1},
{"sr-0eca774977e1","tests/spec/glsl-1.10/preprocessor/elif-1.shader_test","b24859d58762b2ff0127af40f54a720ab67d2f0518df8069c8973c2cd87b605d","",110,0,250,250,0,0,463,1,1566,2},
{"sr-8e1db418a2a2","tests/spec/glsl-1.10/preprocessor/elif-2.shader_test","7a481674c44f1b850bce2294451e018e1172721d22294ccc547eac62b8791a81","",110,0,250,250,0,0,464,1,1568,2},
{"sr-ea66e512f191","tests/spec/glsl-1.10/preprocessor/extension-defined-test.shader_test","4884437522b47a56fd1c26cc9478aeb7bfb75137fe15e2d9c88c2f01ae3d495d","GL_ARB_draw_buffers",110,0,250,250,0,0,465,1,1570,2},
{"sr-f230228aeb13","tests/spec/glsl-1.10/preprocessor/extension-if-1.shader_test","fa145f0c3533743f23c32706a071e2a93ef30912f6d302ca8ac0f757605600e6","GL_ARB_draw_buffers",110,0,250,250,0,0,466,1,1572,2},
{"sr-6409169c2601","tests/spec/glsl-1.10/preprocessor/if-0-define-macro.shader_test","f8eb3edd085136501925a1665ccef8deef455b06b1d2f179752aef169f2a8753","",110,0,250,250,0,0,467,1,1574,2},
{"sr-acec501799b9","tests/spec/glsl-1.10/preprocessor/if-0.shader_test","1159732317ddc537a6fa065ca449f149809685ceed5ed49b8cffbdc5f6cbf553","",110,0,250,250,0,0,468,1,1576,2},
{"sr-05f75c1dfe47","tests/spec/glsl-1.10/preprocessor/if-1-define-macro.shader_test","20ab65af83f92a28a662729c51cd1b67dd6bc6331dc91d47cfd9b901732826c1","",110,0,250,250,0,0,469,1,1578,2},
{"sr-20f94693fbbc","tests/spec/glsl-1.10/preprocessor/if-1-define-nested-macro.shader_test","9902dd8791e8ebfa28e4c2498603e8dfd7c6992462a8daf9f72b30201157231e","",110,0,250,250,0,0,470,1,1580,2},
{"sr-3f31f17adf79","tests/spec/glsl-1.10/preprocessor/if-1.shader_test","5020bdee17fc06dc6e1326307e6a167b492796cd4c9be89ce78b0626605a4194","",110,0,250,250,0,0,471,1,1582,2},
{"sr-642da7c0fbcc","tests/spec/glsl-1.10/preprocessor/if-defined.shader_test","32f426f1fa48e1eff118013acfeae091490401070e2147e2bf1514351e1138f9","",110,0,250,250,0,0,472,1,1584,2},
{"sr-921291feec8c","tests/spec/glsl-1.10/preprocessor/if-eq.shader_test","ba7f597f9d5cf8148c7df1618a845eb99440c7f5a08b26012370f4dbeae22b96","",110,0,250,250,0,0,473,1,1586,2},
{"sr-80336d8872b0","tests/spec/glsl-1.10/preprocessor/ifdef.shader_test","f46697963f0277b92c3ab0c2ed72ff58511ffc2a7e69c4940bb902ae5c01201f","",110,0,250,250,0,0,474,1,1588,2},
{"sr-2b6d97cec6ec","tests/spec/glsl-1.10/preprocessor/ifndef.shader_test","76d28020708de3acf462b2606f503b9fbe6b5ae0d270b034042c8b2a5c766ce0","",110,0,250,250,0,0,475,1,1590,2},
{"sr-96729c9af525","tests/spec/glsl-1.10/preprocessor/multi-line-define.shader_test","e1d74c647ffc249dd574dec9e4391401b2d1000370032935c381fd6c3218ac40","",110,0,250,250,0,0,476,1,1592,2},
{"sr-2e15d63e7f8c","tests/spec/glsl-1.10/preprocessor/nested-if-1.shader_test","fce6bd1ae35ca7411d4ee088a514835760c221b113da46ae0d5dbcdfa1d86880","",110,0,250,250,0,0,477,1,1594,2},
{"sr-bc67559f2a94","tests/spec/glsl-1.10/preprocessor/nested-if-2.shader_test","cfe20e0cbfcfb45cc9c04b0b84ba28da3e71e821b5a7bf9ef213c74843dc09e3","",110,0,250,250,0,0,478,1,1596,2},
{"sr-dad931fcca39","tests/spec/glsl-1.10/preprocessor/nested-if-elif.shader_test","6d025c1be4cb99c3e5ebeb0dae625293d885f91b5a41f2202f68f0669412cdd1","",110,0,250,250,0,0,479,1,1598,2},
{"sr-e1a74e60b9c5","tests/spec/glsl-1.10/preprocessor/not-defined.shader_test","67be4ed3d7f50bb5d397cafa63f96e6049b70d223d28220b06a92891c8410656","",110,0,250,250,0,0,480,1,1600,2},
{"sr-83af7f31a214","tests/spec/glsl-1.20/compiler/unused-const-array.shader_test","245f319f05418c64399e38bc2dea7a019244a0ca32d3e1b3f1a56e0ec4c6d402","",120,0,250,250,0,0,481,2,1602,2},
{"sr-7cfda1a878aa","tests/spec/glsl-1.20/execution/clipping/fixed-clip-enables.shader_test","bae0310a59de4f370a052bd2c4504640c4d1524bba7eaf4dc6d0b11cea7d1913","",120,0,250,250,0,0,483,1,1604,87},
{"sr-637e60d613ba","tests/spec/glsl-1.20/execution/clipping/vs-clip-vertex-const-accept.shader_test","af0cc24fb8088bce6f376af5b5b1c75961b20bfbaf89a3bd3f54a325167c900b","",120,0,250,250,0,0,484,2,1691,6},
{"sr-0eb5fab01bc0","tests/spec/glsl-1.20/execution/clipping/vs-clip-vertex-const-reject.shader_test","0adc0319d50a69405e14413b3c491e91acf7d6337c40b93b0f5d98ef0b95a816","",120,0,250,250,0,0,486,2,1697,6},
{"sr-329552a009ee","tests/spec/glsl-1.20/execution/clipping/vs-clip-vertex-different-from-position.shader_test","77ff096be91bbd9fcd056c04dffd0edba39e6341b7579c0d9f9adcb3dfaefeea","",120,0,250,250,0,0,488,2,1703,28},
{"sr-177063c6cf98","tests/spec/glsl-1.20/execution/clipping/vs-clip-vertex-enables.shader_test","b642d271eac721bf559a6661eb482475b70f7f6a5e0f5f0f1789f6c137d9a747","",120,0,250,250,0,0,490,2,1731,87},
{"sr-924c31b03a98","tests/spec/glsl-1.20/execution/clipping/vs-clip-vertex-equal-to-position.shader_test","f5abd34421c0e71c52c51b615863631b757dd35fc40b98d04f36dd1e5d57b3c7","",120,0,250,250,0,0,492,2,1818,28},
{"sr-f05a00e32ea8","tests/spec/glsl-1.20/execution/clipping/vs-clip-vertex-homogeneity.shader_test","5bbb4bbb134e7863343b24c6bdf454f228217250b93fde817db6f1ba58fe3a97","",120,0,250,250,0,0,494,2,1846,28},
{"sr-dda79206e35c","tests/spec/glsl-1.20/execution/clipping/vs-clip-vertex-primitives.shader_test","4ae50cfae7f67549784d3d92e69f52c2a9416fb48a16d8c7cac0574334cd07dc","",120,0,250,250,0,1,496,2,1874,25},
{"sr-37de5b697b61","tests/spec/glsl-1.20/execution/fs-array-deref-then-write.shader_test","6911107dd8f956b722a5344563ea6d0805c11366a0a543a8e009b1aa2662843d","",120,0,250,250,0,0,498,2,1899,2},
{"sr-b80b72930923","tests/spec/glsl-1.20/execution/fs-assign-varied-struct.shader_test","1f9d269137aada69fe7b9afbaf695a10a011ee24d4ec8cc25ec96490b48984bf","",120,0,250,250,0,0,500,2,1901,2},
{"sr-32cbb498be4d","tests/spec/glsl-1.20/execution/fs-const-array-of-struct-of-array.shader_test","7f445ce2b487aa445c3b499f752d3c2c473e5fd91e42ac33a2b6971e31cbb1a0","",120,0,250,250,0,0,502,2,1903,3},
{"sr-35ca7abbe5ec","tests/spec/glsl-1.20/execution/fs-const-array-of-struct.shader_test","7472ca66ba5a0af346b2f2a55f45141650ba23918bc7848b1a7bcc771e162541","",120,0,250,250,0,0,504,2,1906,3},
{"sr-9c3c9f3c5edc","tests/spec/glsl-1.20/execution/fs-deref-array-of-matrix-element.shader_test","5f5ee02c34a9ceeb3e3f18a0411137d3268bbec115777f98ae93e972e65c46cb","",120,0,250,250,0,0,506,1,1909,2},
{"sr-eb631a302953","tests/spec/glsl-1.20/execution/fs-deref-literal-array-of-structs-vec4-member.shader_test","27eb63e14d0ed71427a7d76806619bfb5375ac04a6fd370845f558d8e204fad9","",120,0,250,250,0,0,507,2,1911,7},
{"sr-75c84f4c3ec4","tests/spec/glsl-1.20/execution/fs-deref-literal-array-of-structs-with-multiple-members-large-array.shader_test","21eadf75b4d93e2f0e9f579fd222d9add8d6af703372d3313aba7e1e606f70a7","",120,0,250,250,0,0,509,2,1918,14},
{"sr-9d5a417c1787","tests/spec/glsl-1.20/execution/fs-deref-literal-array-of-structs-with-multiple-members.shader_test","ebd02ddbec9630989fa7da8f46344936ce1b6ae73c60b624c0b7c52bae230bcb","",120,0,250,250,0,0,511,2,1932,7},
{"sr-861b4846175f","tests/spec/glsl-1.20/execution/fs-deref-literal-array-of-structs.shader_test","bb1ccd13fba904d7b14d84dee787561996d5bbf15731e3fffe021704cf5a7d10","",120,0,250,250,0,0,513,2,1939,7},
{"sr-9f6e3cd6b93c","tests/spec/glsl-1.20/execution/fs-function-inout-array-of-structs.shader_test","8726a2385e12d2f50c5f9275ffada93ca476a60b19b572d29bd8a413bb11fba8","",120,0,250,250,0,0,515,2,1946,2},
{"sr-506b24faf7dd","tests/spec/glsl-1.20/execution/fs-notEqual-const-of-const-bvec3.shader_test","1f63436ae1b6b9b64200ef59c060d164a2ae5694669f697213813d82c1309ba2","",120,0,250,250,0,0,517,2,1948,2},
{"sr-e28e7474c3bc","tests/spec/glsl-1.20/execution/fs-notEqual-const-of-const-vec3.shader_test","ac853b1dd500de05997fab4c9daf88e8178046dc95d6e544724b8514fdfe4a6e","",120,0,250,250,0,0,519,2,1950,2},
{"sr-8062aba267b8","tests/spec/glsl-1.20/execution/fs-vec4-const-array-indirect-access-004-elements.shader_test","fee4ceb250bfdb9231af7cf385559371299643681d58cf216bc9b82a53734325","",120,0,250,250,0,0,521,2,1952,3},
{"sr-b95a2f90f347","tests/spec/glsl-1.20/execution/fs-vec4-const-array-indirect-access-008-elements.shader_test","12236eb01c3be7c228c64516ee5ffc3b420ae01663cc170363cd1988706fffca","",120,0,250,250,0,0,523,2,1955,3},
{"sr-4977be37ad33","tests/spec/glsl-1.20/execution/fs-vec4-const-array-indirect-access-016-elements.shader_test","2d685fc4fabd9583fc68225499f45c08c531c566b13d76e32edfb7b4a4264786","",120,0,250,250,0,0,525,2,1958,3},
{"sr-0a9df72a8c68","tests/spec/glsl-1.20/execution/fs-vec4-const-array-indirect-access-032-elements.shader_test","d123089079f362018717e33d86203df63383ed8132f7f92347052142da0e0631","",120,0,250,250,0,0,527,2,1961,3},
{"sr-ce38dddfc062","tests/spec/glsl-1.20/execution/fs-vec4-const-array-indirect-access-048-elements.shader_test","a5455ebc02596f3a21e7fc8f3fdedb3f5ee45c69c5d63b72a71456e25b3693eb","",120,0,250,250,0,0,529,2,1964,3},
{"sr-c4a6bf69d1a3","tests/spec/glsl-1.20/execution/fs-vec4-const-array-indirect-access-064-elements.shader_test","7faeb2e2bec7ced2a5c540e27c1b365b5e092545b54cc8f1dc1708c3bbaf44cb","",120,0,250,250,0,0,531,2,1967,3},
{"sr-f74298438b41","tests/spec/glsl-1.20/execution/fs-vec4-const-array-indirect-access-128-elements.shader_test","e9182a26b17ee5a210a2631c89a963217e2d05a5e4ce781eca1a99f6372d0c74","",120,0,250,250,0,0,533,2,1970,3},
{"sr-ac884b1c1a21","tests/spec/glsl-1.20/execution/fs-vec4-const-array-indirect-access-256-elements.shader_test","6bf1e863b5a60934592be6fb7ac79a179a5a916700c1e896283cc60e4d089250","",120,0,250,250,0,0,535,2,1973,3},
{"sr-812457cefcd0","tests/spec/glsl-1.20/execution/literal-varyings.shader_test","cc7c8f532f5699027f9328435c912c8490efecb509489570e83769cbbdda44f3","",120,0,250,250,0,0,537,2,1976,2},
{"sr-cdc2dde9581f","tests/spec/glsl-1.20/execution/out-parameter-indexing/fs-inout-index-two-level.shader_test","2b0f001d83f0089b1a79f161da37bd7aec31b491777b0745f3378780536f02ff","",120,0,250,250,0,0,539,2,1978,2},
{"sr-ca3e6800e370","tests/spec/glsl-1.20/execution/out-parameter-indexing/vs-inout-index-inout-float-array.shader_test","246326f4ae4d4bb166af2d9b56bccb191c5333553c630e7d29ec834c4cf57dbe","",120,0,250,250,0,0,541,2,1980,2},
{"sr-47f971f2f7c7","tests/spec/glsl-1.20/execution/out-parameter-indexing/vs-inout-index-inout-mat2-col-array.shader_test","2134e219a508755a3ec344e6b471556a06a5a37e656f0630dc583b6b7863f2be","",120,0,250,250,0,0,543,2,1982,2},
{"sr-f05aa7824a05","tests/spec/glsl-1.20/execution/out-parameter-indexing/vs-inout-index-inout-mat2-col.shader_test","f618ed6954f996bf7709031d219fe73592622ee22198cb7f8035a27cadd36f20","",120,0,250,250,0,0,545,2,1984,2},
{"sr-a4d120c69cfe","tests/spec/glsl-1.20/execution/out-parameter-indexing/vs-inout-index-inout-mat2-row.shader_test","f4ec6775defce2d10d7fe20a8538820307ea1b9c32b746120fbecbece1b2c542","",120,0,250,250,0,0,547,2,1986,2},
{"sr-897190cc2c3f","tests/spec/glsl-1.20/execution/out-parameter-indexing/vs-inout-index-inout-vec4-array-element-array.shader_test","2fb1a1c95b4b0b981661b956b71a781025ba1702301b501513854f3e86b928df","",120,0,250,250,0,0,549,2,1988,2},
{"sr-cbcc315a5052","tests/spec/glsl-1.20/execution/out-parameter-indexing/vs-inout-index-inout-vec4-array-element.shader_test","9d031282d16b75dc23df093227960d2bdbe371566d2c6613e97042b051598ec3","",120,0,250,250,0,0,551,2,1990,2},
{"sr-0ce428fced0d","tests/spec/glsl-1.20/execution/out-parameter-indexing/vs-inout-index-inout-vec4-array.shader_test","578a88ba4098f660d19ee54907d6f1425d62afa96ca6322e754da80726217d3e","",120,0,250,250,0,0,553,2,1992,2},
{"sr-795a48c35cc7","tests/spec/glsl-1.20/execution/out-parameter-indexing/vs-inout-index-inout-vec4.shader_test","e13534bc2807c59bc295fa1c868df54dac96a8c617a72170f3c6dcba241f40dc","",120,0,250,250,0,0,555,2,1994,2},
{"sr-5df63c220fe7","tests/spec/glsl-1.20/execution/out-parameter-indexing/vs-inout-vec4.shader_test","85d0da4cb4540633cb44cad39e3c52ba205521adb2e541c10ef3a507ab8fe648","",120,0,250,250,0,0,557,2,1996,2},
{"sr-26249d97f81a","tests/spec/glsl-1.20/execution/out-parameter-indexing/vs-out-vec4.shader_test","894feb439c4c9b9525ef98e10b69920f2e842361b4c3046284232715733d9b09","",120,0,250,250,0,0,559,2,1998,2},
{"sr-d1e2a182eae9","tests/spec/glsl-1.20/execution/qualifiers/vs-out-conversion-int-int-float-to-float-float-float-return.shader_test","cc1d352303571434d9252a0cf61ddcd080e4d75559d666581ba2e4bb63667eb7","",120,0,250,250,0,0,561,2,2000,2},
{"sr-f3fefafb496d","tests/spec/glsl-1.20/execution/qualifiers/vs-out-conversion-int-to-float-return.shader_test","d732dee4a1c7b97cb8ef93247d53844f4d16e45ec4be1f6dce9a9b4e6d3e9535","",120,0,250,250,0,0,563,2,2002,2},
{"sr-999b6c636901","tests/spec/glsl-1.20/execution/qualifiers/vs-out-conversion-int-to-float-vec4-index.shader_test","1faa5316047f99a645d38671928382846cce67d7fb2c0c7c1f2a6dbba6dec2a2","",120,0,250,250,0,0,565,2,2004,2},
{"sr-da2c5d7d5a7e","tests/spec/glsl-1.20/execution/qualifiers/vs-out-conversion-int-to-float.shader_test","edec3950a622d24421149f25167bbb8f55ea4eefc54bc6a0206bd9a42156ded4","",120,0,250,250,0,0,567,2,2006,2},
{"sr-f93641a4a7ff","tests/spec/glsl-1.20/execution/uniform-initializer/fs-structure-array.shader_test","d10b1fed2e6df0dd8966ab91c30d17a5f0056495725c77d6cc13513c20c4bf66","",120,0,250,250,0,0,569,2,2008,2},
{"sr-909cd7e1399e","tests/spec/glsl-1.20/execution/uniform-initializer/fs-structure.shader_test","ab9704b7d57f03eb7d9f0f65f38ff6c050512fdd3adf5b0276955325a172093c","",120,0,250,250,0,0,571,2,2010,2},
{"sr-a092e78e4187","tests/spec/glsl-1.20/execution/uniform-initializer/vs-structure-array.shader_test","e5af9d879735ce7feb82763d7937b24df68d831f56432bb2b7afec550c105f17","",120,0,250,250,0,0,573,2,2012,2},
{"sr-0d9b63ecbcf0","tests/spec/glsl-1.20/execution/uniform-initializer/vs-structure.shader_test","334e5e99b9f5a4c39c1365500f9104b9a9c7c01359085208a8724b2b2f60c0e2","",120,0,250,250,0,0,575,2,2014,2},
{"sr-c8ad2e9a1272","tests/spec/glsl-1.20/execution/vs-all-equal-bool-array.shader_test","1378ad7f60f326661aaa6a0a1adca2f2ed98d25327660d3088466634cebe4a53","",120,0,250,250,0,0,577,2,2016,2},
{"sr-023e695ff8ff","tests/spec/glsl-1.20/execution/vs-assign-varied-struct.shader_test","a1f6281dfa869c046e241c11d802a7fae0540e59d8ea6a843cfdbabad8f34100","",120,0,250,250,0,0,579,2,2018,2},
{"sr-a7715a0d5f01","tests/spec/glsl-1.20/execution/vs-deref-literal-array-of-structs.shader_test","d39193e29b106852664cc87372f2c059f700d472aff4fa5b498c8bee2294f353","",120,0,250,250,0,0,581,1,2020,7},
{"sr-7908bb1dc156","tests/spec/glsl-1.20/execution/vs-function-inout-array-self-assignment.shader_test","82109dd27c4979c0ee677935b5c3baa2dfdd92ae590f23d32ce34f678452ca33","",120,0,250,250,0,0,582,2,2027,2},
{"sr-73b51be0a1f2","tests/spec/glsl-1.20/execution/vs-mix-as-condition.shader_test","d8cd367367ac877cf148fed72aa1d5a7e96cb041ae549ec4c62393dcdeb03f5e","",120,0,250,250,0,0,584,2,2029,2},
{"sr-b379f0781ab9","tests/spec/glsl-1.20/linker/centroid-mismatched.shader_test","86031cfc2dc87ca5003e064bf630167ff4a6844609c27045a12f7278b7cadf5e","",120,0,250,250,0,0,586,2,2031,1},
{"sr-c89a833ebd4c","tests/spec/glsl-1.20/linker/intrastage-const-arrays.shader_test","dec4b5cb601e62f2b28142a804fa19390c930c1590ac5ef30db9d42cf4305935","",120,0,250,250,0,0,588,2,2032,1},
{"sr-c6c4303d861e","tests/spec/glsl-1.20/linker/intrastage-unsized-array-mismatch.shader_test","15115c8654ad2d964c53adca9d21834b0ca5efdddaf57541e95c4265039d7c86","",120,0,250,250,0,0,590,2,2033,1},
{"sr-ae15453edccc","tests/spec/glsl-1.20/linker/intrastage-unsized-array-mismatch2.shader_test","920e682a049becfbd5e77906e97b2dfd65f7f277b25241794b1c80df4b986be1","",120,0,250,250,0,0,592,2,2034,1},
{"sr-1d49583f97ef","tests/spec/glsl-1.20/linker/intrastage-unsized-array.shader_test","0790656d51f64cae236aa1d46bc3db00f5258feb1a2d92e686f60e64a6a190b2","",120,0,250,250,0,0,594,2,2035,1},
{"sr-dd921b6960fa","tests/spec/glsl-1.20/linker/intrastage-unsized-array2.shader_test","cce7f41e04bb77eb0b2de6ab9cb729708eab368ae04618774854e55d6acb5d84","",120,0,250,250,0,0,596,2,2036,1},
{"sr-c91a71056ce3","tests/spec/glsl-1.20/linker/invariant-mismatched.shader_test","4120503b40f71f3752be876e26d7bbc0a334b35c6ede1df766621b7f9bcd7784","",120,0,250,250,0,0,598,2,2037,1},
{"sr-f5845fd9256e","tests/spec/glsl-1.20/linker/invariant-position.shader_test","081f06c47c4867b7dfc82b00ece11107a617b69574ec056cb76dd99e347529e9","",120,0,250,250,0,0,600,2,2038,1},
{"sr-3d95312249f9","tests/spec/glsl-1.20/linker/link-order-a-b.shader_test","edd39a8f3af029ce2eb640838e2be68dfe70dbd9f18909dc87134f8eff5b3c83","",120,0,250,250,0,0,602,3,2039,2},
{"sr-9341fb7dca32","tests/spec/glsl-1.20/linker/link-order-b-a.shader_test","b8fe93f03e108a961f5f4dcd13b9ca6884305b0407ee1c7796c4c21c990f017c","",120,0,250,250,0,0,605,3,2041,2},
};
