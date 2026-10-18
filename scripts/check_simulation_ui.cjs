// Start simulation_server.py first. Uses a fresh headless browser, never a user profile.
const {chromium}=require(process.env.PLAYWRIGHT_MODULE || 'playwright');
(async()=>{
 const browser=await chromium.launch({channel:'msedge',headless:true});
 try{
  const page=await browser.newPage({viewport:{width:1280,height:820}});
  page.on('pageerror',e=>{throw e;});
  await page.goto(process.env.SIMULATION_URL || 'http://127.0.0.1:8777');
  await page.getByText('4 行 × 4 列 · 16 个单元格',{exact:true}).waitFor();
  await page.getByRole('button',{name:'运行模拟测试'}).click();
  await page.getByText('8 项模拟流程通过；不代表 Illustrator 实机验收通过。',{exact:true}).waitFor();
  console.log('Visible simulation workflow: 8 checks passed');
  await page.getByRole('button',{name:'填入示例文字'}).click();
  await page.getByRole('gridcell',{name:'第1行第1列',exact:true}).dblclick();
  await page.getByText('文字编辑：第 1 行，第 1 列',{exact:true}).waitFor();
  console.log('Double click enters canvas editor');
  await page.locator('#editor').press('Control+A');
  await page.locator('#editor').pressSequentially('TEST');
  await page.getByRole('gridcell',{name:'第1行第1列',exact:true}).filter({hasText:'TEST'}).waitFor();
  await page.locator('#editor').press('Tab');
  await page.getByText('文字编辑：第 1 行，第 2 列',{exact:true}).waitFor();
  console.log('Canvas text input and Tab passed');
  await page.locator('#editor').press('Escape');
  await page.getByRole('gridcell',{name:'第1行第1列',exact:true}).click();
  await page.getByText('选区：第 1–1 行，第 1–1 列',{exact:true}).waitFor();
  await page.getByRole('gridcell',{name:'第3行第3列',exact:true}).click({modifiers:['Shift']});
  await page.getByText('选区：第 1–3 行，第 1–3 列',{exact:true}).waitFor();
  await page.getByRole('button',{name:'平均行高',exact:true}).click();
  await page.getByText('选区：第 1–3 行，第 1–3 列',{exact:true}).waitFor();
  await page.getByRole('button',{name:'平均列宽',exact:true}).click();
  await page.getByText('选区：第 1–3 行，第 1–3 列',{exact:true}).waitFor();
  console.log('Sidebar distribution commands retain the selected region');
  await page.getByRole('gridcell',{name:'第4行第4列',exact:true}).dragTo(page.getByRole('gridcell',{name:'第2行第2列',exact:true}));
  await page.getByText('选区：第 2–4 行，第 2–4 列',{exact:true}).waitFor();
  console.log('Mouse drag selects a reversed rectangular cell region');
  if(process.env.SIMULATION_SCREENSHOT)await page.screenshot({path:process.env.SIMULATION_SCREENSHOT,fullPage:true});
 }finally{await browser.close();}
})().catch(e=>{console.error(e.stack);process.exitCode=1;});
