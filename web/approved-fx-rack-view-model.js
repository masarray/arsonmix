// SPDX-License-Identifier: AGPL-3.0-only
// GUI-2: pure FX Rack presentation/readback. The ONE native mixer owns all
// plugin instances, graph revisions, bypass state and editor HWND lifetime.
(function (root, factory) {
  const api=factory();
  if(typeof module==="object"&&module.exports)module.exports=api;
  if(root)root.ArSonApprovedFxRackViewModel=api;
})(typeof globalThis==="object"?globalThis:undefined,function () {
  "use strict";
  // A rack and the global A/B gate do not rewrite any individual insert flag.
  // Use the exact accepted instance ID for every projected power button.
  function powerPresentation(channel,globalFxBypassed,trackName) {
    if(typeof channel?.fxRackBypassed!=="boolean"||
       !Array.isArray(channel.inserts)||
       typeof globalFxBypassed!=="boolean")return null;
    const enabled=!channel.fxRackBypassed;
    const globalEnabled=!globalFxBypassed;
    const slots=channel.inserts.map((insert,slot)=>{
      if(!insert||typeof insert.id!=="string"||!insert.id||
         typeof insert.bypassed!=="boolean")return null;
      const effective=enabled&&globalEnabled&&!insert.bypassed;
      const latencyTitle=insert.latencyChanged===true?
        "Plugin delay changed · aligned dry signal retained; reload the plugin before using its new delay":
        Number.isInteger(insert.latencySamples)?
          "Plugin delay: "+insert.latencySamples+" samples · compensated":null;
      return {
        slot,instanceId:insert.id,bypassed:insert.bypassed,
        slotEnabled:!insert.bypassed,effective,latencyTitle,
        buttonTitle:(insert.bypassed?"Enable":"Bypass")+" only insert "+(slot+1),
        buttonLabel:(insert.bypassed?"Enable":"Bypass")+" only FX insert "+
          (slot+1)+" in "+trackName
      };
    });
    if(slots.some(slot=>!slot))return null;
    return {
      enabled,globalEnabled,chainBypassed:!enabled||!globalEnabled,
      headerPressed:enabled&&globalEnabled,
      headerTitle:(enabled?"Bypass":"Enable")+" all channel FX",
      headerLabel:(enabled?"Bypass":"Enable")+" entire FX chain in "+trackName,
      slots
    };
  }
  // Pure mounted DOM compatibility guard. Never paint a reused row when its
  // native processor lease or slot position has changed at the same revision.
  function mountedCompatible(projection,rows) {
    if(!projection||!Array.isArray(rows)||
       rows.length!==projection.slots.length)return false;
    const seen=new Set();
    return rows.every(row=>{
      if(!row||typeof row.slot!=="string"||
         seen.has(row.slot)||row.hasPower!==true)return false;
      seen.add(row.slot);
      const slot=projection.slots.find(s=>String(s.slot)===row.slot);
      return !!slot && row.instanceId===slot.instanceId;
    });
  }
  // M4 catalog metadata may change but is NEVER authority to overwrite an
  // installed CHANNEL_ID rack. The bridge enforces that authority boundary.
  function m4Presentation(rows,catalog,acceptedRack) {
    if(!Array.isArray(rows))return null;
    const slots=Array.from({length:8},(_,slot)=>{
      const live=rows[slot];
      const entry=live && catalog?.get?.(live.catalogId);
      const name=live?(entry?.name||"VST3 Loaded"):"None";
      return {slot,name,bypassed:Boolean(live?.bypassed),
        instance:live?{id:live.instanceId,name}:null};
    });
    return {fxOn:typeof acceptedRack==="boolean"?!acceptedRack:null,slots};
  }
  // ACK alone is insufficient. Confirm only the matching instance from the
  // later installed ChannelId graph; a moved/deleted insert must not settle.
  function powerReadbackConfirmed(pending,channels) {
    if(pending?.acked!==true||!Array.isArray(channels))return false;
    const channel=channels.find(ch=>ch.id===pending.channelId);
    if(!channel)return false;
    const value=pending.kind==="rack"?channel.fxRackBypassed:
      pending.kind==="insert"?channel.inserts?.find(i=>i.id===pending.insertId)?.bypassed:
      undefined;
    return typeof value==="boolean"&&value===pending.bypassed;
  }
  function m4RackPendingState(pending,session) {
    if(!pending||session?.authority!=="M4"||
       session.deviceEpoch!==pending.epoch||
       session.routeRevision!==pending.revision)return "STALE";
    return pending.acked===true&&
      session.channels?.[pending.channel]?.fxRackBypassed===pending.bypassed?
      "CONFIRMED":"WAIT";
  }
  return Object.freeze({powerPresentation,mountedCompatible,m4Presentation,
    powerReadbackConfirmed,m4RackPendingState});
});
