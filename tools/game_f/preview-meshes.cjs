const fs = require('node:fs');
const path = require('node:path');
const zlib = require('node:zlib');
const exportAssets=process.argv.includes('--export-ps1');
const springerDetail=exportAssets||process.argv.includes('--springer-detail');
const sniffV3=springerDetail||process.argv.includes('--sniff-v3');
const dogOptions=process.argv.includes('--dog-options');
const springerTail=springerDetail||process.argv.includes('--springer-tail');
const retrieverV2=springerTail||dogOptions||sniffV3||process.argv.includes('--retriever-v2');
const out = path.join(__dirname, springerDetail?'springer-detail-review':springerTail?'springer-tail-review':dogOptions?'dog-options':sniffV3?'retriever-sniff-pass-3':retrieverV2?'retriever-pass-2':'mesh-pass-1');
if (!exportAssets) fs.mkdirSync(out, { recursive: true });

const palette = {
  oak:[93,71,47], birch:[179,169,140], pine:[105,76,49], alder:[93,74,58], willow:[115,98,65],
  oakLeaf:[96,112,51], birchLeaf:[131,137,67], pineLeaf:[67,89,55],
  alderLeaf:[76,99,46], willowLeaf:[112,129,61],
  coat:[143,91,50], saddle:[77,52,40], tan:[173,114,66], cream:[185,153,103],
  ear:[98,60,41], nose:[47,35,30], eye:[31,27,22],
  duck:[130,111,82], duckHead:[56,92,65], bill:[158,127,52],
  fur:[112,76,54], muzzle:[135,101,73], earFur:[88,56,39],
  mouth:[56,39,30], iris:[104,70,35], glint:[169,145,109],
  duckWing:[99,93,86], duckSpeculum:[55,65,82], featherEdge:[169,153,125],
  labCoat:[155,113,68], labMuzzle:[167,132,92], labEar:[115,76,44],
  springerWhite:[184,175,151], springerLiver:[91,54,41],
  pointerWhite:[179,172,151], pointerLiver:[77,49,42], pointerTick:[130,116,98],
  earFold:[72,42,32], springerTick:[140,115,92]
};
const add = (a,b) => a.map((x,i)=>x+b[i]);
const sub = (a,b) => a.map((x,i)=>x-b[i]);
const scale = (a,s) => a.map(x=>x*s);
const cross = (a,b) => [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];
const unit = a => scale(a,1/(Math.hypot(...a)||1));
const mix = (a,b,t) => add(scale(a,1-t),scale(b,t));
function mesh(name) { return {name, vertices:[], faces:[]}; }
function ring(m, points) {
  const start=m.vertices.length;
  m.vertices.push(...points);
  return points.map((_,i)=>start+i);
}
function bridge(m,a,b,material) {
  a.forEach((v,i)=>{
    const next=(i+1)%a.length, key=Array.isArray(material)?material[i]:material;
    m.faces.push({v:[v,a[next],b[next]],uv:[[0,0],[1,0],[1,1]],material:key},
      {v:[v,b[next],b[i]],uv:[[0,0],[1,1],[0,1]],material:key});
  });
}
function branch(m, points, radii, material, sides=5, reference) {
  let previous;
  points.forEach((p,i)=>{
    const axis=unit(sub(points[Math.min(i+1,points.length-1)],points[Math.max(i-1,0)]));
    const u=unit(cross(axis,reference??(Math.abs(axis[2])>.9?[0,1,0]:[0,0,1]))), v=cross(axis,u);
    const next=ring(m,Array.from({length:sides},(_,j)=>{
      const a=j*Math.PI*2/sides;
      return add(p,scale(add(scale(u,Math.cos(a)),scale(v,Math.sin(a))),radii[i]));
    }));
    if(previous){
      const first=m.faces.length;
      bridge(m,previous,next,material);
      if(exportAssets) for(let i=first;i<m.faces.length;i++) m.faces[i].tube=true;
    }
    previous=next;
  });
  if(retrieverV2 && material==='nose'){
    const center=ring(m,[points.at(-1)])[0];
    previous.forEach((v,i)=>m.faces.push({v:[center,v,previous[(i+1)%sides]],material}));
  }
}
function card(m,p,w,h,yaw,material,seed,roll=0) {
  const u=[Math.cos(yaw),0,Math.sin(yaw)], up=[0,1,0];
  const horizontal=scale(add(scale(u,Math.cos(roll)),scale(up,Math.sin(roll))),w/2);
  const vertical=scale(sub(scale(up,Math.cos(roll)),scale(u,Math.sin(roll))),h/2);
  const v=ring(m,[sub(sub(p,horizontal),vertical),add(sub(p,vertical),horizontal),
    add(add(p,horizontal),vertical),add(sub(p,horizontal),vertical)]);
  m.faces.push({v:[v[0],v[1],v[2]],uv:[[0,1],[1,1],[1,0]],material,seed},
    {v:[v[0],v[2],v[3]],uv:[[0,1],[1,0],[0,0]],material,seed});
}
const trees=[
  ['oak', [[0,0,0],[-.08,1.2,0],[.04,2.7,0],[-.35,4.0,.1]],
    [[1.45,-1.4,2.7,.2,1.45,1.0],[2.2,1.45,3.22,-.4,1.55,1.05],
     [2.67,-1.05,3.76,-.65,1.5,1.04],[3.25,.55,4.1,.65,1.45,1.12],
     [1.97,.27,3.15,1.02,1.2,.85],[3.52,-.4,4.35,.0,1.25,.9]]],
  ['birch', [[-.18,0,0],[-.26,1.7,0],[-.49,3.45,.1],[-.58,4.72,.1]],
    [[1.82,-.95,2.97,.22,.83,.95],[2.5,-.03,3.75,-.35,.82,1],
     [3.08,-1.05,4.12,-.26,.9,1.03],[3.6,-.63,4.78,.2,.88,1.12],
     [2.65,.62,3.86,.25,.76,1],[3.32,.73,4.39,-.2,.83,1.15]]],
  ['pine', [[0,0,0],[.13,1.65,0],[-.01,3.35,0],[.15,4.96,.07]],
    [[1.75,-1.13,2.19,.18,1.4,.49],[2.23,1.05,2.53,-.28,1.4,.55],
     [2.9,-.98,3.1,-.38,1.36,.57],[3.38,.93,3.6,.16,1.26,.59],
     [3.77,-.68,4.06,.26,1.12,.58],[4.19,.59,4.47,-.12,.95,.6],
     [4.6,.15,4.97,.08,.8,.63]]],
  ['alder', [[-.14,0,0],[-.48,1.32,.1],[-.64,2.77,.1],[-.72,3.85,.21]],
    [[1.07,-.96,2.14,.11,.94,1.02],[1.66,.16,2.5,.69,1.0,.97],
     [2.23,-.79,3.01,-.23,1.15,1.0],[2.88,-.61,3.84,.22,.94,.98],
     [1.94,.81,2.75,-.08,1.0,1.14],[2.42,.55,3.48,-.33,1.0,1.08]]],
  ['willow', [[0,0,0],[.12,1.04,0],[.47,2.06,.1],[.89,3.06,.25]],
    [[1.21,-.83,2.31,.09,1.1,1.37],[1.7,.07,2.64,.7,1.1,1.53],
     [2.12,.7,3.19,.42,1.25,1.39],[2.32,1.69,2.86,-.26,1.13,1.52],
     [1.58,1.37,2.32,.48,1.02,1.37],[2.66,1.2,3.36,.05,1.03,1.55]]]
];
function makeTree([type,trunk,branches]) {
  const m=mesh(type), size={oak:.37,birch:.11,pine:.23,alder:.17,willow:.29}[type];
  branch(m,trunk,[size,size*.67,size*.39,size*.06],type,exportAssets?4:6);
  if(type==='birch'||type==='alder')
    branch(m,[[.02,0,-.12],[.34,1.54,-.1],[.49,2.88,-.12],[.65,trunk[3][1]-.31,-.17]],
      [size*.87,size*.58,size*.37,size*.04],type,5);
  if(type==='alder') branch(m,[[.04,0,.1],[.05,1.42,.28],[.29,2.87,.55]],[.13,.08,.008],type);
  const atHeight=y=>{
    const i=trunk.findIndex(p=>p[1]>=y);
    return mix(trunk[i-1],trunk[i],(y-trunk[i-1][1])/(trunk[i][1]-trunk[i-1][1]));
  };
  branches.forEach(([y,x,tipY,z,w,h],i)=>{
    const start=atHeight(y), end=[x,tipY,z], fork=mix(start,end,.58);
    fork[1]+=.16;
    branch(m,[start,fork,end],[size*.36,size*.20,.01],type,exportAssets?3:4);
    const secondary=add(mix(fork,end,.62),[i%2?.22:-.22,.2,i%2?-.36:.36]);
    if(!exportAssets) branch(m,[fork,secondary],[size*.15,.006],type,3);
    const center=add(end,[0,type==='willow'?-.27:.05,0]);
    const spread=type==='oak'?1.36:type==='alder'?1.23:type==='pine'?1.20:1.16;
    card(m,center,w*spread,h*1.23,(i%3-1)*.27,type+'Leaf',i,.14*(i%3-1));
    card(m,secondary,w*spread*.92,h*.93,1.2+i*.23,type+'Leaf',i+17,-.11);
    if(type==='oak'||type==='alder'||type==='willow')
      card(m,add(mix(end,secondary,.47),[i%2?.12:-.12,.21,0]),
        w*.87,h*.94,.39+i*.17,type+'Leaf',i+33,.12);
  });
  return m;
}

function body(m, profiles, materials) {
  let previous;
  for (const [z,y,w,h] of profiles) {
    const next=ring(m,Array.from({length:8},(_,i)=>{
      const a=(i+.5)*Math.PI/4;
      return [Math.sin(a)*w,y+Math.cos(a)*h,z];
    }));
    if(previous) bridge(m,previous,next,materials);
    previous=next;
  }
}
function dog(pose) {
  const m=mesh('retriever-'+pose);
  const sniff=pose==='sniff', swim=pose==='swim'||pose==='retrieve', step=pose==='walk';
  const h=swim?-.83:0, n=sniff?-.71:0;
  const torso=[[ -.78,.74,.025,.03],[-.64,.74,.21,.23],[-.4,.77,.25,.23],
    [-.04,.77,.24,.23],[.28,.81,.29,.31],[.49,.87,.235,.28],[.68,1.01+n*.42,.155,.19],
    [.85,1.14+n,.138,.15],[.97,1.22+n,.19,.185],[1.12,1.20+n,.178,.16],
    [1.24,1.13+n,.135,.11],[1.44,1.115+n,.088,.074],[1.475,1.115+n,.055,.048]];
  body(m,torso.map(([z,y,w,d])=>[z,y+h,w,d]),['saddle','coat','tan','cream','cream','tan','coat','saddle']);
  for(const side of [-1,1]){
    const ear=[
      [side*.137,1.34+n+h,1.01],[side*.223,1.28+n+h,1.09],[side*.216,1.045+n+h,1.10],
      [side*.182,.96+n+h,1.02],[side*.151,1.06+n+h,.88],[side*.193,1.24+n+h,.88]];
    const v=ring(m,ear), tip=ring(m,[[side*.232,1.16+n+h,.99]])[0];
    v.forEach((index,i)=>m.faces.push({v:[index,v[(i+1)%v.length],tip],material:'ear'}));
    branch(m,[[side*.15,1.251+n+h,1.148],[side*.157,1.25+n+h,1.179]],[.026,.014],'eye',5);
    if(swim) continue;
    const s=step?side*.17:0;
    branch(m,[[side*.197,.78,.33],[side*.204,.49,.38+s],[side*.207,.19,.35+s],
      [side*.208,.055,.42+s],[side*.208,.045,.50+s]],[.134,.094,.051,.072,.034],'tan');
    branch(m,[[side*.191,.79,-.49],[side*.208,.54,-.36-s],[side*.202,.28,-.68-s],
      [side*.209,.062,-.58-s],[side*.209,.048,-.49-s]],[.157,.113,.062,.074,.034],'coat');
  }
  branch(m,[[0,1.116+n+h,1.424],[0,1.119+n+h,1.50],[0,1.119+n+h,1.516]],[.086,.057,.012],'nose',6);
  branch(m,[[0,.88+h,-.64],[.02,.89+h,-.91],[.075,.83+h,-1.18],[.11,.85+h,-1.43]],
    [.104,.069,.041,.008],'saddle',6);
  if(pose==='retrieve'){
    const cy=.19, cz=1.52;
    branch(m,[[-.42,cy-.06,cz],[-.21,cy-.04,cz],[.08,cy-.035,cz],[.3,cy-.02,cz]],
      [.024,.105,.125,.06],'duck',7);
    branch(m,[[.24,cy,cz],[.36,cy-.02,cz],[.46,cy-.05,cz]],[.065,.07,.038],'duckHead',6);
    branch(m,[[.44,cy-.065,cz],[.59,cy-.1,cz]],[.04,.022],'bill',5);
  }
  return m;
}

function retriever(pose, continuousTail=false, strideSign=1) {
  const m=mesh('retriever-'+pose);
  const sniff=pose==='sniff', swim=pose==='swim'||pose==='retrieve', walk=pose==='walk';
  const sink=swim?.89:0;
  const neck=sniff?(sniffV3?.86:.78):0;
  const headY=swim?.03:0;
  const noseDown=([x,y,z],weight=1)=>{
    if(!sniff||!sniffV3||!weight) return [x,y,z];
    const angle=.39*weight, c=Math.cos(angle), s=Math.sin(angle);
    const hingeY=1.18-neck, hingeZ=.91;
    return [x,hingeY+(y-hingeY)*c-(z-hingeZ)*s,
      hingeZ+(z-hingeZ)*c+(y-hingeY)*s];
  };
  const head=[
    [.66,1.01-neck*.46,.155,.203],[.78,1.10-neck*.72,.151,.196],
    [.89,1.195-neck,.177,.197],[1.015,1.23-neck,.208,springerDetail?.205:.189],
    [1.10,1.205-neck,.189,.169],[1.175,1.15-neck,.153,.114],
    [1.24,1.127-neck,.128,.088],[1.365,1.121-neck,.104,.078],
    [1.415,1.119-neck,.083,.066]
  ];
  let previous;
  const rump=continuousTail ? [
    [-1.145,.895,.007,.008],[-1.06,.882,.023,.022],[-.97,.86,.033,.027],
    [-.88,.847,.041,.037],[-.79,.832,.062,.061],[-.71,.803,.116,.117],
    [-.626,.787,.193,.189]
  ] : [[-.785,.755,.04,.071],[-.675,.777,.183,.176]];
  const profiles=[
    ...rump,[-.53,.785,.235,.225],
    [-.33,.815,.206,.204],[-.12,.83,.210,.207],[.10,.831,.247,.266],
    [.31,.84,.253,.302],[.48,.88-neck*.10,.218,.272],...head
  ];
  const sides=exportAssets?8:10;
  for(const [z,y,w,h] of profiles){
    const next=ring(m,Array.from({length:sides},(_,i)=>{
      const a=(i+.5)*2*Math.PI/sides;
      const cheek=z>1&&z<1.18&&Math.cos(a)<0?1.11:1;
      return noseDown([Math.sin(a)*w*cheek,y-sink+(z>.85?headY:0)+Math.cos(a)*h,z],
        Math.max(0,Math.min(1,(z-.73)/.19)));
    }));
    if(previous) bridge(m,previous,next,z>1.23?'muzzle':'fur');
    previous=next;
  }
  const face=(x,y,z)=>noseDown([x,y-neck-sink+headY,z]);
  for(const side of [-1,1]){
    const p=(x,y,z)=>face(side*x,y,z);
    const ear=ring(m,[
      p(.138,1.349,.94),p(.184,1.323,1.059),p(.225,1.25,1.054),
      p(.236,1.131,1.00),p(.21,1.024,.963),p(.169,1.012,.885),
      p(.157,1.102,.847),p(.170,1.245,.86)
    ]);
    const fold=ring(m,[p(.246,1.20,.949),p(.205,1.076,.932)]);
    ear.forEach((v,i)=>m.faces.push({v:[v,ear[(i+1)%ear.length],fold[i>=3&&i<=5?1:0]],material:'earFur'}));
    m.faces.push({v:[ear[3],fold[0],fold[1]],material:'earFur'},
      {v:[ear[6],fold[0],fold[1]],material:'earFur'});
    const eye=ring(m,[p(.178,1.248,1.127),p(.185,1.265,1.092),p(.194,1.246,1.07),
      p(.188,1.225,1.100)]);
    const pupil=ring(m,[p(.19,1.244,1.102),p(.192,1.253,1.104),p(.187,1.252,1.118)]);
    m.faces.push({v:[eye[0],eye[1],eye[2]],material:'eye'},
      {v:[eye[0],eye[2],eye[3]],material:'iris'},{v:pupil,material:'glint'});
    const jaw=ring(m,[p(.146,1.085,1.16),p(.121,1.084,1.239),p(.103,1.083,1.354),
      p(.103,1.075,1.354),p(.121,1.075,1.239),p(.146,1.077,1.16)]);
    m.faces.push({v:[jaw[0],jaw[1],jaw[5]],material:'mouth'},{v:[jaw[5],jaw[1],jaw[4]],material:'mouth'},
      {v:[jaw[1],jaw[2],jaw[4]],material:'mouth'},{v:[jaw[4],jaw[2],jaw[3]],material:'mouth'});
    if(swim) continue;
    const s=walk?side*strideSign*.19:0;
    const lift=walk&&side===strideSign?.09:0;
    const rearLift=exportAssets ? (walk&&side!==strideSign?.075:0) : lift*.42;
    const legSides=exportAssets?5:6;
    let front=[[side*.178,.883,.354],[side*.209,.658,.306],[side*.207,.433,.335+s*.52],
      [side*.207,.18+lift,.369+s],[side*.207,.075+lift,.397+s],
      [side*.207,.049+lift,.49+s]];
    let rear=[[side*.161,.837,-.55],[side*.19,.666,-.45],[side*.208,.481,-.41-s*.25],
      [side*.215,.237,-.63-s*.72],[side*.218,.079+rearLift,-.586-s],
      [side*.218,.048+rearLift,-.485-s]];
    if(pose==='launch'||pose==='tuck'||pose==='reach'){
      const fore=[
        [[.46,.55],[.47,.76],[.43,.92],[.43,1.01]],
        [[.52,.45],[.46,.26],[.45,.32],[.43,.39]],
        [[.43,.52],[.23,.63],[.10,.67],[.07,.73]]
      ];
      const hind=[
        [[.43,-.53],[.23,-.78],[.12,-.94],[.10,-1.01]],
        [[.46,-.30],[.35,-.49],[.33,-.34],[.31,-.25]],
        [[.49,-.34],[.32,-.58],[.25,-.51],[.23,-.43]]
      ];
      const frame=pose==='launch'?0:pose==='tuck'?1:2;
      front=front.map(([x,y,z],i)=>i<2?[x,y,z]:[x,fore[frame][i-2][0],fore[frame][i-2][1]+side*.024]);
      rear=rear.map(([x,y,z],i)=>i<2?[x,y,z]:[x,hind[frame][i-2][0],hind[frame][i-2][1]+side*.033]);
    }
    if(pose==='sit'){
      rear=[[side*.16,.82,-.55],[side*.21,.60,-.42],[side*.23,.40,-.27],
        [side*.24,.18,-.53],[side*.23,.073,-.43],[side*.23,.045,-.34]];
    }
    const legStart=m.vertices.length;
    const legFrame=exportAssets?[1,0,0]:undefined;
    branch(m,front,[.139,.113,.076,.046,.065,.033],'fur',legSides,legFrame);
    branch(m,rear,[.152,.141,.108,.053,.064,.031],'fur',legSides,legFrame);
    if(exportAssets) (m.featherLegs??=[]).push({side,legStart,legSides});
  }
  branch(m,[face(0,1.123,1.385),face(0,1.125,1.43),face(0,1.126,1.455)],[.078,.068,.023],'nose',7);
  if(!continuousTail)
    branch(m,[[0,.868-sink,-.645],[.017,.868-sink,-.83],[.044,.815-sink,-1.02],
      [.096,.772-sink,-1.20],[.148,.772-sink,-1.34]],[.111,.098,.072,.043,.008],'fur',6);
  if(pose==='retrieve'){
    const cy=.19, cz=1.46;
    branch(m,[[-.49,cy-.07,cz-.09],[-.29,cy-.033,cz-.02],[.04,cy-.012,cz],
      [.23,cy+.014,cz+.015]],[.024,.105,.143,.064],'duck',7);
    branch(m,[[.18,cy+.025,cz+.01],[.28,cy+.01,cz+.04],
      [.38,cy-.08,cz+.105],[.454,cy-.09,cz+.116]],[.045,.07,.065,.033],'duckHead',6);
    branch(m,[[.419,cy-.098,cz+.112],[.563,cy-.122,cz+.15]],[.038,.018],'bill',5);
    for(const side of [-1,1]){
      const wing=ring(m,[[.123,cy+.08,cz+side*.08],[-.06,cy+.051,cz+side*.144],
        [-.27,cy-.008,cz+side*.112],[-.412,cy-.047,cz+side*.05],
        [-.095,cy-.074,cz+side*.103],[-.01,cy-.026,cz+side*.152]]);
      m.faces.push({v:[wing[0],wing[1],wing[5]],material:'duckWing'},
        {v:[wing[1],wing[2],wing[5]],material:'duckSpeculum'},
        {v:[wing[2],wing[3],wing[4]],material:'duckWing'},
        {v:[wing[2],wing[4],wing[5]],material:'featherEdge'});
    }
  }
  return m;
}

function noise(x,y,s=0) {
  let n=Math.imul(x+29,374761393)^Math.imul(y+17,668265263)^Math.imul(s+9,1442695041);
  n=Math.imul(n^(n>>>13),1274126177);
  return ((n^(n>>>16))>>>0)/4294967296;
}

function alternateDog(kind,pose='stand',strideSign=1) {
  const m=retriever(pose,springerTail&&kind==='springer',strideSign);
  m.name=kind;
  m.coat=kind;
  const ears=new Set(m.faces.filter(f=>f.material==='earFur').flatMap(f=>f.v));
  for(const f of m.faces){
    if(f.material==='earFur'){
      f.material=kind==='lab'?'labEar':kind==='springer'?'springerLiver':'pointerLiver';
      if(springerDetail) f.ear=true;
    }
    if(f.material==='muzzle'){
      f.material=kind==='lab'?'labMuzzle':kind==='springer'?'springerWhite':'pointerWhite';
      if(springerDetail) f.muzzle=true;
    }
  }
  m.vertices=m.vertices.map(([x,y,z],i)=>{
    const head=Math.max(0,Math.min(1,(z-.72)*5));
    const tail=Math.max(0,Math.min(1,(-z-.78)*3));
    const foot=Math.max(0,Math.min(1,(.65-y)/.60));
    if(kind==='lab'){
      return [x*(1.20-.09*foot+.06*head),y*.97+.038*head,
        z-.115*Math.max(0,z-1.06)*2+tail*.07];
    }
    if(kind==='springer'){
      return [x*(1.03-.16*foot)+(ears.has(i)?Math.sign(x)*.014:0),
        y*.84-(ears.has(i)?Math.max(0,1.32-(y+(pose==='sniff'?.86:0)))*.55:0)+(springerTail?0:tail*.10),
        z*.88-.22*Math.max(0,z-1.11)+(springerTail?0:tail*.21)];
    }
    const chest=z>.02&&z<.48?Math.max(0,1-Math.abs(z-.25)*5):0;
    return [x*(.82-.10*foot),y*1.13-.125*chest*Math.max(0,1-Math.abs(y-.59)*4)+tail*.12,
      z*1.04+Math.max(0,z-1.17)*.18-tail*.10];
  });
  if(springerDetail) featherSpringer(m,ears,pose,strideSign);
  if(pose==='sit'){
    m.vertices=m.vertices.map(([x,y,z])=>{
      const haunch=Math.max(0,Math.min(1,(-z+.17)/.72));
      const chest=Math.max(0,Math.min(1,(z-.03)/.65));
      const turn=Math.max(0,Math.min(1,(z-.63)*5))*.44;
      const ahead=z-.80;
      return [x*Math.cos(turn)-ahead*Math.sin(turn)+x*haunch*.10,
        y*(1-haunch*.61)+chest*Math.min(1,y/.7)*.075,
        .80+ahead*Math.cos(turn)+x*Math.sin(turn)+haunch*.12-chest*Math.min(1,y/.77)*.14];
    });
  }
  if(pose==='crouch')
    m.vertices=m.vertices.map(([x,y,z])=>[x,y*(z<-.10?.72:.81),z]);
  return m;
}
function featherSpringer(m,ears,pose,strideSign){
  function tuft(a,b,tip,material,ear=false){
    const v=ring(m,[a,b,tip,add(mix(a,b,.5),[Math.sign(tip[0])*.009,-.013,0])]);
    m.faces.push({v:[v[0],v[1],v[3]],material,ear},
      {v:[v[1],v[2],v[3]],material,ear},{v:[v[2],v[0],v[3]],material,ear});
  }
  for(const side of [-1,1]){
    const edge=[...ears].map(i=>m.vertices[i]).filter(v=>v[0]*side>0)
      .sort((a,b)=>a[1]-b[1]);
    const tip=edge[0], low=edge[1], outer=edge[2];
    tuft(low,tip,add(mix(tip,low,.42),[side*.012,-.035,-.012]),'springerLiver',true);
    tuft(tip,outer,add(mix(tip,outer,.55),[side*.016,-.035,-.035]),'springerLiver',true);
    tuft(add(tip,[side*.012,.015,.014]),add(outer,[side*.014,.025,0]),
      add(mix(tip,outer,.52),[side*.025,.005,.004]),'earFold',true);
    const walk=pose==='walk'?side*strideSign*.19:0;
    const leg=m.featherLegs?.find(l=>l.side===side);
    for(const [i,[y,z]] of [[.49,.32+walk*.53],[.39,.34+walk*.69],[.29,.35+walk*.85],
      [.53,-.43-walk*.24],[.43,-.49-walk*.4],[.33,-.55-walk*.58]].entries()){
      if(leg){
        const start=leg.legStart+(i<3 ? i+2 : i+5)*leg.legSides;
        const p=m.vertices.slice(start,start+leg.legSides).reduce((a,b)=>a[2]<b[2]?a:b);
        tuft(p,add(p,[side*.017,-.035,.015]),add(p,[side*.019,-.05,-.052]),'springerWhite');
        continue;
      }
      const x=side*(y>.43?.22:.195);
      tuft([x,y*.84,z*.88-.033],[x+side*.017,(y-.05)*.84,z*.88-.044],
        [x+side*.021,(y-.087)*.84,z*.88-.099],'springerWhite');
    }
    const neckDrop=pose==='sniff'?.11:0;
    tuft([side*.08,.59-neckDrop,.49],[side*.14,.56-neckDrop,.45],
      [side*.116,.48-neckDrop,.456],'springerWhite');
  }
  tuft([-.018,.69,-.77],[.019,.69,-.77],[.007,.65,-.858],'springerLiver');
  tuft([-.012,.715,-.86],[.016,.715,-.86],[.01,.685,-.923],'springerLiver');
  if(pose==='sniff'){
    for(const i of new Set(m.faces.filter(f=>f.ear).flatMap(f=>f.v))){
      const [x,y,z]=m.vertices[i];
      if(y>=.145) continue;
      const fold=(.145-y)/.206;
      m.vertices[i]=[x+Math.sign(x)*fold*.05,y+fold*fold*.077,z-fold*.025];
    }
  }
}
function breedCoat(kind,x,y,z){
  if(kind==='lab') return 'labCoat';
  const springer=kind==='springer';
  const scaleY=springer?.84:1.13;
  const height=y/scaleY;
  const freckle=noise(Math.floor(x*83+z*41),Math.floor(y*101),7);
  const wave=Math.sin(z*18+x*11)*.017;
  if(springer&&springerTail&&z<-.72) return z<-.955+wave?'springerWhite':'springerLiver';
  if(z>(springer?.72:.88)) {
    const stripe=(springer?.050:.042)+Math.max(0,z-1)*.055;
    if(Math.abs(x)>stripe+wave||height<1.06) return springer?'springerLiver':'pointerLiver';
  } else {
    const patch=(center,span,base)=>((z-center)/span)**2+((height-base+wave)/.25)**2<1;
    if((springer&&patch(-.13,.63,.98))||patch(-.60,.27,.96)||
      (!springer&&patch(.26,.20,.94))) return springer?'springerLiver':'pointerLiver';
  }
  if(springer&&springerDetail&&height<.67&&freckle>.91) return 'springerTick';
  if(!springer&&height<.67&&freckle>.89) return 'pointerTick';
  return springer?'springerWhite':'pointerWhite';
}
function foliage(key,x,y,seed) {
  const u=(x-31.5)/31.5, v=(y-31.5)/31.5;
  const grain=noise(x,y,seed), patch=noise(x>>2,y>>2,seed);
  let shape;
  if(key==='pineLeaf') {
    shape=1.04-Math.abs(u)*.61-Math.abs(v-u*.21)*1.66;
  } else if(key==='willowLeaf') {
    shape=1.09-Math.abs(u)*.94-Math.abs(v+.24)*.58;
    if(v>-.15 && noise((x+Math.floor(y/12))>>1,0,seed)<.40) return null;
  } else {
    const centers=[[-.52,.08],[.1,-.32],[.48,.02],[-.14,.35],[.25,.35]];
    shape=Math.max(...centers.map(([a,b])=>1-Math.hypot((u-a)*1.55,(v-b)*1.85)));
    if(key==='birchLeaf') shape-=.075;
  }
  if(shape<.10+patch*.31+grain*.18) return null;
  return .66+patch*.44+grain*.18-v*.10;
}
function materialSample(face,u,v) {
  const key=face.material, x=Math.floor(u*63), y=Math.floor(v*63);
  if(key.endsWith('Leaf')) return foliage(key,x,y,face.seed);
  const coarse=noise(x>>2,y>>2,9), fine=noise(x,y,3);
  if(trees.some(t=>t[0]===key)) {
    if(key==='birch' && noise(x>>3,y>>1,2)>.87) return .49;
    return .72+noise((x+(y>>3))>>1,y>>4,5)*.37+fine*.16;
  }
  return .88+coarse*.16+fine*.10;
}
function surface(w,h) {
  const rgb=Buffer.alloc(w*h*3), depth=new Float32Array(w*h).fill(-Infinity);
  for(let y=0;y<h;y++) for(let x=0;x<w;x++){
    const f=(y/h)*.055, i=(y*w+x)*3;
    [114,108,100].forEach((c,j)=>rgb[i+j]=c*(1-f));
  }
  return {w,h,rgb,depth};
}
function view(m, screen, {yaw=0, tilt=.09, zoom=50, x=screen.w/2, y=screen.h*.89, water=false}={}) {
  const c=Math.cos(yaw), s=Math.sin(yaw), cp=Math.cos(tilt), sp=Math.sin(tilt);
  const project=([a,b,d])=>{
    const xx=a*c-d*s, zz=d*c+a*s;
    return [x+xx*zoom,y-(b*cp-zz*sp)*zoom,b*sp+zz*cp];
  };
  const projected=m.vertices.map(project), {w,h,rgb,depth}=screen;
  if(water) {
    for(let py=Math.max(0,Math.floor(y));py<h;py++) for(let px=0;px<w;px++){
      const ripple=noise(px>>3,py>>1,8);
      const i=(py*w+px)*3;
      [105,126,111].forEach((v,j)=>rgb[i+j]=v+Math.floor(ripple*18));
    }
  }
  for(const face of m.faces) {
    const [a,b,d]=face.v.map(i=>projected[i]);
    const area=(b[0]-a[0])*(d[1]-a[1])-(b[1]-a[1])*(d[0]-a[0]);
    if(Math.abs(area)<.01) continue;
    const minX=Math.max(0,Math.ceil(Math.min(a[0],b[0],d[0]))),
      maxX=Math.min(w-1,Math.floor(Math.max(a[0],b[0],d[0])));
    const minY=Math.max(0,Math.ceil(Math.min(a[1],b[1],d[1]))),
      maxY=Math.min(h-1,Math.floor(Math.max(a[1],b[1],d[1])));
    const [v0,v1,v2]=face.v.map(i=>m.vertices[i]);
    const n=unit(cross(sub(v1,v0),sub(v2,v0)));
    const light=face.material.endsWith('Leaf')?.99:.79+Math.abs(n[0]*.56+n[1]*.72+n[2]*.37)*.31;
    const uv=face.uv??[[0,0],[1,0],[.5,1]], color=palette[face.material];
    for(let py=minY;py<=maxY;py++) for(let px=minX;px<=maxX;px++){
      const B=((px-a[0])*(d[1]-a[1])-(py-a[1])*(d[0]-a[0]))/area;
      const D=((b[0]-a[0])*(py-a[1])-(b[1]-a[1])*(px-a[0]))/area;
      const A=1-B-D;
      if(Math.min(A,B,D)<-.00001) continue;
      if(water && A*v0[1]+B*v1[1]+D*v2[1]<0) continue;
      const i=py*w+px, z=A*a[2]+B*b[2]+D*d[2];
      if(z<=depth[i]) continue;
      let shade=materialSample(face,A*uv[0][0]+B*uv[1][0]+D*uv[2][0],
        A*uv[0][1]+B*uv[1][1]+D*uv[2][1]);
      if(shade===null) continue;
      let tint=color;
      if(face.muzzle && noise(Math.floor((A*uv[0][0]+B*uv[1][0]+D*uv[2][0])*57),
        Math.floor((A*uv[0][1]+B*uv[1][1]+D*uv[2][1])*53),17)>.965)
        tint=palette.springerTick;
      if(face.material==='fur'){
        const y=A*v0[1]+B*v1[1]+D*v2[1]+(water?.89:0);
        const x=A*v0[0]+B*v1[0]+D*v2[0], z=A*v0[2]+B*v1[2]+D*v2[2];
        if(m.coat) {
          tint=palette[breedCoat(m.coat,x,y,z)];
          if(springerDetail && m.coat==='springer'){
            const direction=noise(Math.floor((x+y*.45)*117),Math.floor((z-y*.55)*33),12);
            shade*=.94+direction*.12;
          }
        }
        const saddle=z<.64&&z>-.86?Math.max(0,Math.min(1,(y-.86)*6)):0;
        const chest=z>.29&&z<.61&&y>.54&&y<.88?Math.max(0,1-Math.abs(x)*5):0;
        shade*=1.05-(m.coat?.10:.33)*saddle+.22*chest+.19*Math.max(0,.72-y);
      }
      depth[i]=z;
      tint.forEach((v,j)=>rgb[i*3+j]=Math.min(255,Math.floor(v*light*shade/4)*4));
    }
  }
}
function crc(buffer){
  let sum=~0;
  for(const byte of buffer){
    sum^=byte;
    for(let i=0;i<8;i++) sum=(sum>>>1)^((sum&1)?0xedb88320:0);
  }
  return (~sum)>>>0;
}
function chunk(name,data){
  const key=Buffer.from(name), body=Buffer.concat([key,data]), head=Buffer.alloc(4), tail=Buffer.alloc(4);
  head.writeUInt32BE(data.length); tail.writeUInt32BE(crc(body));
  return Buffer.concat([head,body,tail]);
}
function png(name,screen){
  const {w,h,rgb}=screen, header=Buffer.alloc(13), scan=Buffer.alloc(h*(w*3+1));
  header.writeUInt32BE(w);header.writeUInt32BE(h,4);header[8]=8;header[9]=2;
  for(let y=0;y<h;y++) rgb.copy(scan,y*(w*3+1)+1,y*w*3,(y+1)*w*3);
  fs.writeFileSync(path.join(out,name+'.png'),Buffer.concat([Buffer.from('89504e470d0a1a0a','hex'),
    chunk('IHDR',header),chunk('IDAT',zlib.deflateSync(scan)),chunk('IEND',Buffer.alloc(0))]));
}
function sheet(name,panels,columns){
  const w=panels[0].w,h=panels[0].h, margin=4;
  const result=surface(columns*(w+margin)-margin,Math.ceil(panels.length/columns)*(h+margin)-margin);
  panels.forEach((p,i)=>{
    for(let y=0;y<h;y++){
      const dest=(((h+margin)*Math.floor(i/columns)+y)*result.w+(i%columns)*(w+margin))*3;
      p.rgb.copy(result.rgb,dest,y*w*3,(y+1)*w*3);
    }
  });
  png(name,result);
}
function enlarge(screen,factor) {
  const result=surface(screen.w*factor,screen.h*factor);
  for(let y=0;y<result.h;y++) for(let x=0;x<result.w;x++){
    const i=(Math.floor(y/factor)*screen.w+Math.floor(x/factor))*3;
    screen.rgb.copy(result.rgb,(y*result.w+x)*3,i,i+3);
  }
  return result;
}
if(springerDetail){
  const stand=alternateDog('springer'), sniff=alternateDog('springer','sniff');
  const walk=[alternateDog('springer','walk'),alternateDog('springer','walk',-1)];
  if(exportAssets){
    const sit=alternateDog('springer','sit');
    const leap=['crouch','launch','tuck','reach'].map(p=>alternateDog('springer',p));
    const q=n=>Math.round(n*64);
    const rows=(name,values)=>'static const '+name+' = {\n'+values.map(v=>' {'+v.join(',')+'}').join(',\n')+'\n};\n';
    const earIndices=new Set(stand.faces.filter(f=>f.ear).flatMap(f=>f.v));
    let c='/* Generated from the approved Game F review meshes. */\n';
    const poses=[stand,sniff,...walk,sit,...leap];
    const packedPoses=poses.map(m=>m.vertices.map(v=>v.map(q)));
    if(packedPoses.flat(2).some(v=>v<-128||v>127)) throw new Error('Springer coordinate exceeds signed byte');
    c+=rows('signed char springer_pose['+poses.length+']['+stand.vertices.length+'][3]',
      packedPoses.map(m=>m.map(v=>'{'+v.join(',')+'}')));
    c+=rows('unsigned short springer_face[][6]',stand.faces.map(f=>{
      const p=scale(f.v.map(i=>stand.vertices[i]).reduce(add),1/3);
      const tint=palette[f.material==='fur'?breedCoat('springer',...p):f.material];
      const [a,b,d]=f.v.map(i=>stand.vertices[i]);
      const n=unit(cross(sub(b,a),sub(d,a)));
      const light=.82+Math.abs(n[0]*.56+n[1]*.72+n[2]*.37)*.30;
      const rgb=tint.map(v=>Math.min(31,Math.round(v*light/8)));
      const body=f.v.every(i=>i<(17+5)*8);
      const v=f.tube?[f.v[0],f.v[2],f.v[1]]:f.v;
      return [...v,rgb[0]|rgb[1]<<5|rgb[2]<<10,body||f.tube?0:1,
        f.v.some(i=>stand.vertices[i][1]>=.70)?1:0];
    }));
    c+='static const unsigned char springer_ears['+stand.vertices.length+'] = {'+[...earIndices]
      .map(i=>'['+i+']='+Math.round(Math.max(0,Math.min(1,(1.04-stand.vertices[i][1])/.24))*255))
      .join(',')+'};\n';
    c+='static const unsigned char springer_nod[] = {'+stand.vertices
      .map(v=>Math.round(255*Math.max(0,Math.min(1,(v[2]-.50)/.22))))
      .join(',')+'};\n';
    const variants=trees.map(makeTree);
    for(const m of variants){
      const trunk=m.faces.filter(f=>!f.material.endsWith('Leaf'));
      c+=rows('short tree_'+m.name+'_points[][3]',m.vertices.map(v=>v.map(q)));
      c+=rows('unsigned short tree_'+m.name+'_bark[][3]',trunk.map(f=>[f.v[0],f.v[2],f.v[1]]));
      const cards=m.faces.filter(f=>f.material.endsWith('Leaf'));
      c+=rows('short tree_'+m.name+'_cards[][5]',cards.filter((_,i)=>!(i&1)).map((f,i)=>{
        const corners=[...new Set([...f.v,...cards[i*2+1].v])].map(v=>m.vertices[v]);
        const mid=scale(corners.reduce(add),.25);
        return [...mid.map(q),q(Math.hypot(...sub(corners[1],corners[0]))),q(Math.hypot(...sub(corners[3],corners[0])))];
      }));
    }
    c+='static const unsigned int approved_leaf_masks[14][512] = {\n';
    const slots=[...variants.map(m=>({name:m.name})),{name:'grass',style:0},{name:'grass',style:1},
      ...variants.map(m=>({name:'distant',model:m})),{name:'mist'},{name:'flowers'}];
    const flowers=[[10,28,5,7],[22,16,5,10],[32,26,4,12],[46,12,6,7],[54,30,4,10]];
    for(const {name,style,model} of slots){
      const blades=name==='grass'?Array.from({length:style?54:70},(_,i)=>({
        base:2+noise(i,31+style)*60, height:12+noise(i,63+style)*(style?50:37),
        bend:(noise(i,17+style)-.5)*(style?26:16), tint:5+Math.floor(noise(i,83+style)*8)
      })):[];
      const tile=model?surface(64,64):null;
      if(tile){
        const top=Math.max(...model.vertices.map(v=>v[1]));
        view(model,tile,{yaw:.29,tilt:0,zoom:54/top,y:61});
      }
      const pixels=Array.from({length:4096},(_,i)=>{
        if(name==='flowers'){
          const x=i&63,y=i>>6;
          let shade=0;
          for(const [head,top,radius,color] of flowers){
            const dy=y-top, dx=x-head;
            if(dy>=0){
              const stem=head+(32-head)*(dy/(63-top))*.28;
              if(Math.abs(x-stem)<1) shade=2+(y&1);
              if(y>top+12 && y<61 && Math.abs(x-stem)<8 &&
                 (y+Math.floor(Math.abs(x-stem))*2)%14<2) shade=4;
            }
            if(dx*dx+dy*dy<=radius*radius){
              if(Math.abs(dx)<2 && Math.abs(dy)<2) return 15;
              if(Math.abs(dx)+Math.abs(dy)<radius+2) shade=color+(dy>0?0:1);
            }
          }
          return shade;
        }
        if(name==='mist'){
          const x=i&63,y=i>>6;
          const edge=Math.sin(x*Math.PI/63);
          const curl=Math.sin(x*.15)*5+Math.cos(x*.29)*2;
          const alpha=edge*Math.max(0,1-Math.abs(y-31-curl)/(11+edge*12));
          return noise(x,y,92)<alpha*.80 ? 4+Math.floor(alpha*9) : 0;
        }
        if(tile){
          if(!Number.isFinite(tile.depth[i])) return 0;
          const [r,g]=tile.rgb.subarray(i*3,i*3+2);
          return g>r ? 8+Math.min(7,Math.floor(g/16)) : 1+Math.min(6,Math.floor(r/26));
        }
        if(name==='grass'){
          const y=63-(i>>6), x=i%64;
          let shade=0;
          for(const b of blades){
            const t=y/b.height;
            if(t>1) continue;
            if(Math.abs(x-b.base-b.bend*t*t)<1.23-t*.86) shade=Math.max(shade,b.tint);
          }
          return shade;
        }
        const value=foliage(name+'Leaf',i%64,i>>6,0);
        return value===null?0:Math.max(1,Math.min(15,Math.round(value*10)));
      });
      const words=Array.from({length:512},(_,i)=>pixels.slice(i*8,i*8+8)
        .reduce((a,v,j)=>a|(v<<(j*4)),0)>>>0);
      c+=' {\n'+Array.from({length:64},(_,i)=>'  '+words.slice(i*8,i*8+8)
        .map(v=>'0x'+v.toString(16)+'u').join(',')).join(',\n')+'\n },\n';
    }
    c+='};\n';
    c+='static const uint32_t forest_floor_texels[2048] = {\n';
    const soil=Array.from({length:128*128},(_,i)=>{
      const x=i&127,y=i>>7;
      const canopy=noise(x>>4,y>>4,33)*.58+noise(x>>2,y>>2,17)*.42;
      const dirt=noise((x+Math.floor(y/4))>>3,y>>3,57);
      const grit=noise(x,y,82);
      const fleck=noise(x>>1,y>>1,19);
      if(grit>.94&&fleck>.56) return 13+Math.floor(noise(x,y,27)*3);
      if(dirt+canopy*.37<.47) return 1+Math.min(4,Math.floor((canopy*.65+grit*.35)*5));
      return 6+Math.min(6,Math.floor((canopy*.65+fleck*.24+grit*.11)*8));
    });
    const ground=Array.from({length:2048},(_,i)=>soil.slice(i*8,i*8+8)
      .reduce((a,v,j)=>a|(v<<(j*4)),0)>>>0);
    c+=Array.from({length:256},(_,i)=>' '+ground.slice(i*8,i*8+8)
      .map(v=>'0x'+v.toString(16)+'u').join(',')).join(',\n');
    c+='\n};\n';
    if (!process.argv[3]) throw new Error('Supply the output header path after --export-ps1.');
    fs.writeFileSync(process.argv[3],c);
    process.exit(0);
  }
  const panels=[[stand,1.57],[stand,.58],[stand,2.55],[sniff,1.57]].map(([m,yaw],i)=>{
    const panel=surface(305,195);
    view(m,panel,{yaw,tilt:i===0||i===3?0:.12,zoom:97,y:169});
    return panel;
  });
  sheet('springer-detail-views',panels,4);
  const close=[.64,1.35].map(yaw=>{
    const panel=surface(300,192), zoom=235;
    view(stand,panel,{yaw,tilt:.06,zoom,x:150+.94*Math.sin(yaw)*zoom,y:306});
    return panel;
  });
  sheet('springer-face-detail',close,2);
  const ears=new Set(stand.faces.filter(f=>f.ear).flatMap(f=>f.v));
  const ease=t=>{const v=Math.max(0,Math.min(1,t));return v*v*(3-2*v);};
  for(let frame=0;frame<48;frame++){
    const step=Math.sin(frame*Math.PI/6)*(1-ease((frame-13)/6));
    const head=ease((frame-17)/10)-ease((frame-36)/9);
    const ear=ease((frame-19)/10)-ease((frame-38)/9);
    const m={...stand,vertices:stand.vertices.map((p,i)=>{
      let v=mix(p,walk[step>=0?0:1].vertices[i],Math.abs(step));
      v=add(v,scale(sub(sniff.vertices[i],p),head));
      if(ears.has(i)){
        const free=Math.max(0,Math.min(1,(1.04-p[1])/.24));
        v[1]+=(head-ear)*free*.10;
        v[2]-=(head-ear)*free*.12;
      }
      const tail=Math.max(0,(-p[2]-.65)/.36);
      v[0]+=tail*tail*Math.sin(frame*.56)*.11;
      return v;
    })};
    const panel=surface(300,160);
    view(m,panel,{yaw:1.24,tilt:.025,zoom:94,y:136});
    png('motion-'+String(frame).padStart(2,'0'),panel);
  }
  for(const [name,m] of [['stand',stand],['sniff',sniff],['walk-left',walk[0]],['walk-right',walk[1]]]){
    fs.writeFileSync(path.join(out,'springer-'+name+'.mesh.json'),JSON.stringify(m));
    console.log(name,m.vertices.length,m.faces.length);
  }
  process.exit(0);
}
if(springerTail){
  const model=alternateDog('springer');
  const full=[1.57,2.27,2.87].map((yaw,i)=>{
    const panel=surface(330,212);
    view(model,panel,{yaw,tilt:i?.10:0,zoom:106,y:181});
    return panel;
  });
  sheet('springer-tail-views',full,3);
  const close=[1.57,2.26].map(yaw=>{
    const panel=surface(320,202), zoom=245;
    view(model,panel,{yaw,tilt:.07,zoom,x:160-.73*Math.sin(yaw)*zoom,y:284});
    return panel;
  });
  sheet('springer-tail-closeup',close,2);
  fs.writeFileSync(path.join(out,'springer.mesh.json'),JSON.stringify(model));
  console.log(model.name,model.vertices.length,model.faces.length);
  process.exit(0);
}
if(dogOptions){
  const breeds=['lab','springer','pointer'];
  const meshes=breeds.map(alternateDog);
  const angles=[1.57,.64,2.63];
  const rows=angles.flatMap((yaw,angle)=>meshes.map((m,i)=>{
    const panel=surface(333,205);
    view(m,panel,{yaw,tilt:angle?.13:0,zoom:93,x:169,y:174});
    png('dog-'+(i+1)+'-'+['side','front','rear'][angle],panel);
    return panel;
  }));
  sheet('three-dog-models',rows,3);
  const small=meshes.map(m=>{
    const panel=surface(106,70);
    view(m,panel,{yaw:2.63,tilt:.2,zoom:22,y:56});
    return enlarge(panel,2);
  });
  sheet('trail-scale',small,3);
  for(const m of meshes){
    fs.writeFileSync(path.join(out,m.name+'.mesh.json'),JSON.stringify(m));
    console.log(m.name,m.vertices.length,m.faces.length);
  }
  process.exit(0);
}
const assets=retrieverV2?[]:trees.map(makeTree);
const treePanels=assets.map((m,i)=>{
  const panel=surface(240,312);
  view(m,panel,{zoom:48,yaw:[.33,.16,-.16,.25,.35][i],y:293});
  png('T'+(i+1)+'-'+m.name,panel);
  return panel;
});
if(treePanels.length) sheet('trees-meshes',treePanels,5);
const views=[
  ['side','stand',1.57,false],['front','stand',.54,false],['rear','walk',2.62,false],
  ['sniff','sniff',1.25,false],['swim','swim',.73,true],['retrieve','retrieve',.76,true]
];
const poses=Object.fromEntries([...new Set(views.map(v=>v[1]))].map(p=>[p,retrieverV2?retriever(p):dog(p)]));
const dogPanels=views.map(([name,pose,yaw,water],i)=>{
  const panel=surface(288,201);
  view(poses[pose],panel,{yaw,tilt:.17,zoom:80,x:151,y:water?138:171,water});
  png('D'+(i+1)+'-'+name,panel);
  return panel;
});
sheet('dog-meshes',dogPanels,3);
const gameplay=views.slice(2).map(([name,pose,yaw,water],i)=>{
  const panel=surface(106,70);
  view(poses[pose],panel,{yaw,tilt:.20,zoom:[22,27,32,32][i],y:water?41:56,water});
  return enlarge(panel,2);
});
sheet('dog-gameplay-scale-study',gameplay,4);
if(sniffV3){
  const panels=[1.57,1.06].map(yaw=>{
    const panel=surface(328,184), ground=150;
    for(let y=ground;y<panel.h;y++) for(let x=0;x<panel.w;x++){
      const shade=noise(x>>2,y>>1,6), i=(y*panel.w+x)*3;
      [89,81,66].forEach((v,j)=>panel.rgb[i+j]=v+shade*12);
    }
    view(poses.sniff,panel,{yaw,tilt:0,zoom:98,x:164,y:ground});
    return panel;
  });
  sheet('sniff-ground-check',panels,2);
}
if(retrieverV2){
  const closeups=[.60,1.52].map(yaw=>{
    const panel=surface(330,235);
    view(poses.stand,panel,{yaw,tilt:.055,zoom:268,
      x:165+1.09*Math.sin(yaw)*268,y:423});
    return panel;
  });
  sheet('dog-head-detail',closeups,2);
}
for(const m of [...assets,...Object.values(poses)]) {
  fs.writeFileSync(path.join(out,m.name+'.mesh.json'),JSON.stringify(m));
  console.log(m.name,m.vertices.length,m.faces.length);
}
