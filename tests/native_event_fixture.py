"""Shared test-only JUCE event fan-out; no PCM or native authority simulated.

The real JUCE backend supports multiple listeners per event and removal by
subscription token. Keep the existing callable dispatch map for smoke tests.
"""
NATIVE_EVENTS = """(()=>{
 const subscriptions=new Map(); let nextId=0;
 window.__nativeListeners=new Map();
 window.__nativeListenerCount=name=>subscriptions.get(name)?.size||0;
 window.__nativeListenerMethods={
  addEventListener(name,callback){
   if(!subscriptions.has(name)){
    subscriptions.set(name,new Map());
    window.__nativeListeners.set(name,payload=>{
     for(const fn of [...subscriptions.get(name).values()])fn(payload);
    });
   }
   const id=++nextId;
   subscriptions.get(name).set(id,callback);
   return {name,id};
  },
  removeEventListener(token){
   subscriptions.get(token?.name)?.delete(token?.id);
  }
 };
})();\n"""
